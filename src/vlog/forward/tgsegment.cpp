#include <vlog/tgsegment.h>
#include <vlog/tgsegmentitr.h>
#include <vlog/segment.h>

std::unique_ptr<TGSegmentItr> TGSegmentLegacy::iterator() const {
    return std::unique_ptr<TGSegmentItr>(new TGSegmentLegacyItr(columns, trackProvenance));
}

bool TGSegmentLegacy::isSortedBy(uint8_t field) const {
    return (isSorted && sortedField == field);
}

std::shared_ptr<TGSegment> TGSegmentLegacy::sortBy(uint8_t field) const {
    if (columns.size() == 1) {
        auto column = columns[0]->sort();
        std::vector<std::shared_ptr<Column>> columns;
        columns.push_back(column);
        return std::shared_ptr<TGSegment>(new TGSegmentLegacy(columns, column->size(), true, 0, trackProvenance));
    } else {
        //General case. Might want to optimize it for binary columns
        auto nfields = columns.size();
        auto oldcols(columns);
        Segment s(nfields, oldcols);
        auto snew = s.sortByField(field);
        std::vector<std::shared_ptr<Column>> newColumns;
        for(int i = 0; i < nfields; ++i) {
            newColumns.push_back(snew->getColumn(i));
        }
        return std::shared_ptr<TGSegment>(new TGSegmentLegacy(newColumns, s.getNRows(), true, field, trackProvenance));
    }
}

std::unique_ptr<TGSegment> TGSegmentLegacy::unique() const {
    if (!isSorted || sortedField != 0) {
        LOG(ERRORL) << "unique can only be called on sorted segments";
        throw 10;
    }

    auto nfields = columns.size();
    auto oldcols(columns);
    std::shared_ptr<Segment> s = std::shared_ptr<Segment>(new Segment(nfields, oldcols));
    auto retained = SegmentInserter::unique(s);
    std::vector<std::shared_ptr<Column>> newcols;
    for(int i = 0; i < retained->getNColumns(); ++i) {
        newcols.push_back(retained->getColumn(i));
    }
    return std::unique_ptr<TGSegment>(new TGSegmentLegacy(newcols, retained->getNRows(), true, sortedField, trackProvenance));
}

std::unique_ptr<TGSegment> TGSegmentLegacy::sort() const {
    if (!isSorted || sortedField != 0) {
        auto nfields = columns.size();
        auto oldcols(columns);
        Segment s(nfields, oldcols);
        auto news = s.sortBy(NULL);
        std::vector<std::shared_ptr<Column>> newcols;
        for(int i = 0; i < news->getNColumns(); ++i) {
            newcols.push_back(news->getColumn(i));
        }
        return std::unique_ptr<TGSegment>(new TGSegmentLegacy(newcols, nrows, true, 0, trackProvenance));
    } else {
        return std::unique_ptr<TGSegment>(new TGSegmentLegacy(columns, nrows, true, 0, trackProvenance));
    }
}

void TGSegmentLegacy::appendTo(uint8_t colPos, std::vector<Term_t> &out) const {
    assert(colPos < columns.size());
    auto &col = columns[colPos];
    const auto vector = col->getVectorRef();
    std::copy(vector.begin(), vector.end(), std::back_inserter(out));
}

void TGSegmentLegacy::appendTo(uint8_t colPos,
        std::vector<std::pair<Term_t, Term_t>> &out) const {
    assert(trackProvenance);
    auto &c1 = columns[colPos];
    auto &v1 = c1->getVectorRef();
    auto &c2 = columns[columns.size() - 1]; //Last column is the provenance
    auto itrProv = c2->getReader();
    auto nrows = v1.size();
    for(size_t i = 0; i < nrows; ++i) {
        if (!itrProv->hasNext())
            throw 10;
        out.push_back(std::make_pair(v1[i], itrProv->next()));
    }
}

void TGSegmentLegacy::appendTo(uint8_t colPos1,
        uint8_t colPos2,
        std::vector<std::pair<Term_t,Term_t>> &out) const {
    assert(columns.size() == 2);
    auto &c1 = columns[colPos1];
    auto &c2 = columns[colPos2];
    auto &v1 = c1->getVectorRef();
    auto &v2 = c2->getVectorRef();
    auto nrows = v1.size();
    for(size_t i = 0; i < nrows; ++i) {
        out.push_back(std::make_pair(v1[i], v2[i]));
    }
}

void TGSegmentLegacy::appendTo(uint8_t colPos1,
        uint8_t colPos2,
        std::vector<BinWithProv> &out) const {
    assert(columns.size() == 3);
    assert(trackProvenance);
    auto &c1 = columns[colPos1];
    auto &c2 = columns[colPos2];
    auto &c3 = columns[2];
    auto &v1 = c1->getVectorRef();
    auto &v2 = c2->getVectorRef();
    auto itrProv = c3->getReader();
    auto nrows = v1.size();
    for(size_t i = 0; i < nrows; ++i) {
        BinWithProv v;
        v.first = v1[i];
        v.second = v2[i];
        if (!itrProv->hasNext()) {
            throw 10;
        }
        v.node = itrProv->next();
        out.push_back(v);
    }
}

std::unique_ptr<TGSegment> TGSegmentLegacy::swap() const {
    if (trackProvenance) {
        if (columns.size() != 3) {
            LOG(ERRORL) << "Not supposed to be invoked on non binary predicates";
            throw 10;
        }
        std::vector<std::shared_ptr<Column>> c;
        c.push_back(columns[1]);
        c.push_back(columns[0]);
        c.push_back(columns[2]);
        return std::unique_ptr<TGSegment>(new TGSegmentLegacy(c, nrows, false, 0, trackProvenance));
    } else {
        if (columns.size() != 2) {
            LOG(ERRORL) << "Not supposed to be invoked on non binary predicates";
            throw 10;
        }
        std::vector<std::shared_ptr<Column>> c;
        c.push_back(columns[1]);
        c.push_back(columns[0]);
        return std::unique_ptr<TGSegment>(new TGSegmentLegacy(c, nrows, false, 0, trackProvenance));
    }
}