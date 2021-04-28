#include <vlog/gbchase/gblegacysegment.h>
#include <vlog/gbchase/gbsegmentitr.h>

#include <vlog/segment.h>

std::unique_ptr<TGSegmentItr> TGSegmentLegacy::iterator(
        std::shared_ptr<const TGSegment> selfref) const {
    return std::unique_ptr<TGSegmentItr>(
            new TGSegmentLegacyItr(columns, trackProvenance));
}

bool TGSegmentLegacy::isSortedBy(std::vector<uint8_t> &fields) const {
    if (fields.size() != 1)
        return false;
    auto field = fields[0];
    return (f_isSorted && sortedField == field);
}

std::vector<std::shared_ptr<const TGSegment>> TGSegmentLegacy::sliceByNodes(
        size_t startNodeIdx,
        std::vector<size_t> &provNodes) const
{
    std::vector<std::shared_ptr<const TGSegment>> out;
    size_t startidx = 0;
    auto itr = iterator();
    size_t i = 0;
    size_t currentNode = ~0ul;
    while (itr->hasNext()) {
        itr->next();
        bool hasChanged = i == 0 || currentNode != itr->getNodeId();
        if (hasChanged) {
            if (startidx < i) {
                //Create a new node
                provNodes.push_back(currentNode);
                auto dataToAdd = slice(
                        startNodeIdx++, startidx, i);
                out.push_back(dataToAdd);
            }
            startidx = i;
            //assert(itr->getNodeId() != ~0ul);
            currentNode = itr->getNodeId();
        }
        i++;
    }
    //Copy the last segment
    if (startidx < i) {
        auto dataToAdd = slice(startNodeIdx++, startidx, i);
        provNodes.push_back(currentNode);
        out.push_back(dataToAdd);
    }
    return out;
}

std::shared_ptr<TGSegment> TGSegmentLegacy::sortBy(std::vector<uint8_t> &fields) const {
    if (f_isSorted && (fields.size() == 0 || (fields.size() == 1 && fields[0] == 0))) {
        return std::shared_ptr<TGSegment>(new TGSegmentLegacy(columns, nrows, true, 0, trackProvenance));
    }
    if (columns.size() == 1) {
        auto column = columns[0]->sort();
        std::vector<std::shared_ptr<Column>> columns;
        columns.push_back(column);
        return std::shared_ptr<TGSegment>(
                new TGSegmentLegacy(
                    columns, column->size(), true, 0, trackProvenance));
    } else {
        auto nfields = columns.size();
        auto oldcols(columns);
        Segment s(nfields, oldcols);
        auto snew = s.sortBy(&fields);
        std::vector<std::shared_ptr<Column>> newColumns;
        for(int i = 0; i < nfields; ++i) {
            newColumns.push_back(snew->getColumn(i));
        }
        return std::shared_ptr<TGSegment>(
                new TGSegmentLegacy(
                    newColumns, s.getNRows(), true, fields[0], trackProvenance));
    }
}

std::shared_ptr<const TGSegment> TGSegmentLegacy::unique() const {
    if (!f_isSorted || sortedField != 0) {
        LOG(ERRORL) << "unique can only be called on sorted segments";
        throw 10;
    }

    size_t nfields = 0;
    size_t nfieldsToCheck = 0;
    std::vector<std::shared_ptr<Column>> oldcols;
    if (trackProvenance && columns.back()->isConstant()) {
        nfields = columns.size() - 1;
        nfieldsToCheck = -1;
        for(int i = 0; i < columns.size() - 1; ++i)
            oldcols.push_back(columns[i]);
    } else {
        nfields = columns.size();
        nfieldsToCheck = trackProvenance ? nfields - 1 : -1; //-1 means check all fields
        oldcols = columns;
    }

    std::shared_ptr<Segment> s = std::shared_ptr<Segment>(
            new Segment(nfields, oldcols));
    auto retained = SegmentInserter::unique(s, nfieldsToCheck);

    std::vector<std::shared_ptr<Column>> newcols;
    for(int i = 0; i < retained->getNColumns(); ++i) {
        newcols.push_back(retained->getColumn(i));
    }
    size_t nrows = retained->getNRows();
    if (trackProvenance && columns.back()->isConstant()) {
        newcols.push_back(std::shared_ptr<Column>(new CompressedColumn(
                        getNodeId(), nrows)));
    }

    return std::shared_ptr<const TGSegment>(new TGSegmentLegacy(newcols,
                nrows, true, sortedField, trackProvenance));
}

std::shared_ptr<const TGSegment> TGSegmentLegacy::sort() const {
    if (!f_isSorted || sortedField != 0) {
        auto nfields = columns.size();
        std::vector<std::shared_ptr<Column>> newcols;
        if (trackProvenance && columns.back()->isConstant()) {
            std::vector<std::shared_ptr<Column>> oldcols;
            for(int i = 0; i < columns.size() - 1; ++i) {
                oldcols.push_back(columns[i]);
            }
            Segment s(columns.size() - 1, oldcols);
            auto news = s.sortBy(NULL);
            for(int i = 0; i < news->getNColumns(); ++i) {
                newcols.push_back(news->getColumn(i));
            }
            newcols.push_back(columns.back());
        } else {
            auto oldcols(columns);
            Segment s(nfields, oldcols);
            auto news = s.sortBy(NULL);
            for(int i = 0; i < news->getNColumns(); ++i) {
                newcols.push_back(news->getColumn(i));
            }
        }
        return std::shared_ptr<const TGSegment>(
                new TGSegmentLegacy(newcols, nrows, true, 0, trackProvenance));
    } else {
        return std::shared_ptr<const TGSegment>(
                new TGSegmentLegacy(columns, nrows, true, 0, trackProvenance));
    }
}

std::shared_ptr<TGSegment> TGSegmentLegacy::sortByProv(size_t ncols,
        std::vector<size_t> &idxs,
        std::vector<size_t> &nodes) const {
    assert(trackProvenance == true);
    assert(columns.size() > 1);
    if (!columns.back()->isConstant()) {
        std::vector<uint8_t> sortedFields;
        sortedFields.push_back(columns.size() - 1);
        for(int i = 0; i < columns.size() - 1; ++i) {
            sortedFields.push_back(i);
        }
        auto nfields = columns.size();
        auto oldcols(columns);
        Segment s(nfields, oldcols);
        auto news = s.sortBy(&sortedFields);
        std::vector<std::shared_ptr<Column>> newcols;
        for(int i = 0; i < news->getNColumns(); ++i) {
            newcols.push_back(news->getColumn(i));
        }
        return std::shared_ptr<TGSegment>(
                new TGSegmentLegacy(newcols, nrows, true, 0, trackProvenance));
    } else {
        return std::shared_ptr<TGSegment>(
                new TGSegmentLegacy(columns, nrows, f_isSorted,
                    sortedField, trackProvenance));
    }
}

std::shared_ptr<TGSegment> TGSegmentLegacy::slice(const size_t nodeId,
        const size_t start,
        const size_t end) const {
    std::vector<std::shared_ptr<Column>> newcols;
    auto length = end - start;
    int ncols = trackProvenance ? columns.size() - 1 : columns.size();
    for(int i = 0; i < ncols; ++i) {
        if (start > 0 || end < nrows - 1) {
            auto c = columns[i]->slice(start, end);
            //auto &v = columns[i]->getVectorRef();
            //std::vector<Term_t> slicedColumn(length);
            //std::copy(v.begin() + start, v.begin() + end, slicedColumn.begin());
            //std::shared_ptr<Column> c(new InmemoryColumn(slicedColumn, true));
            newcols.push_back(c);

        } else {
            newcols.push_back(columns[i]);
        }
    }
    if (trackProvenance) {
        CompressedColumnBlock b(nodeId, 0, length);
        std::vector<CompressedColumnBlock> blocks;
        blocks.push_back(b);
        newcols.push_back(std::shared_ptr<Column>(
                    new CompressedColumn(blocks, length)));
    }
    return std::shared_ptr<TGSegment>(new TGSegmentLegacy(newcols, length, f_isSorted, sortedField, trackProvenance));

}

void TGSegmentLegacy::appendTo(uint8_t colPos, std::vector<Term_t> &out) const {
    assert(colPos < columns.size());
    auto &col = columns[colPos];
    auto itr = col->getReader();
    while (itr->hasNext()) {
        out.push_back(itr->next());
    }
}

void TGSegmentLegacy::appendTo(uint8_t colPos,
        std::vector<std::pair<Term_t, Term_t>> &out) const {
    assert(trackProvenance);
    auto &c1 = columns[colPos];
    auto itr1 = c1->getReader();
    auto &c2 = columns[columns.size() - 1]; //Last column is the provenance
    auto itrProv = c2->getReader();
    auto nrows = c1->size();
    for(size_t i = 0; i < nrows; ++i) {
        if (!itrProv->hasNext())
            throw 10;
        out.push_back(std::make_pair(itr1->next(), itrProv->next()));
    }
}

void TGSegmentLegacy::appendTo(uint8_t colPos1,
        uint8_t colPos2,
        std::vector<std::pair<Term_t,Term_t>> &out) const {
    auto &c1 = columns[colPos1];
    auto &c2 = columns[colPos2];
    auto itr1 = c1->getReader();
    auto itr2 = c2->getReader();
    auto nrows = c1->size();
    for(size_t i = 0; i < nrows; ++i) {
        out.push_back(std::make_pair(itr1->next(), itr2->next()));
    }
}

void TGSegmentLegacy::appendTo(uint8_t colPos1,
        uint8_t colPos2,
        std::vector<BinWithProv> &out) const {
    assert(trackProvenance);
    assert(columns.size() > colPos1);
    assert(columns.size() > colPos2);
    auto &c1 = columns[colPos1];
    auto &c2 = columns[colPos2];
    auto &c3 = columns.back();
    auto itr1 = c1->getReader();
    auto itr2 = c2->getReader();
    auto itrProv = c3->getReader();
    auto nrows = c1->size();
    for(size_t i = 0; i < nrows; ++i) {
        BinWithProv v;
        v.first = itr1->next();
        v.second = itr2->next();
        if (!itrProv->hasNext()) {
            throw 10;
        }
        v.node = itrProv->next();
        out.push_back(v);
    }
}

void TGSegmentLegacy::appendTo(const std::vector<int> &posFields,
        std::vector<std::vector<Term_t>> &out,
        bool withProv) const {
    //The current implementation does not copy the nodeID, thus the size of
    //out should reflect the positions
    if (!withProv) {
        assert(posFields.size() == out.size());
    } else {
        assert(posFields.size() == out.size() - 1);
    }
    size_t i = 0;
    for(auto pos : posFields) {
        auto c = columns[pos];
        auto itr = c->getReader();
        while (itr->hasNext()) {
            out[i].push_back(itr->next());
        }
        i++;
    }
    if (trackProvenance) { //Add the nodeID
        auto &o = out.back();
        auto itr = columns.back()->getReader();
        if (posFields.size() > 0) {
            while (itr->hasNext()) {
                o.push_back(itr->next());
            }
        } else {
            if (itr->hasNext()) {
                o.push_back(itr->next());
            }
        }
    }
}

std::shared_ptr<TGSegment> TGSegmentLegacy::swap() const {
    if (trackProvenance) {
        if (columns.size() != 3) {
            LOG(ERRORL) << "Not supposed to be invoked on non binary predicates";
            throw 10;
        }
        std::vector<std::shared_ptr<Column>> c;
        c.push_back(columns[1]);
        c.push_back(columns[0]);
        c.push_back(columns[2]);
        return std::shared_ptr<TGSegment>(new TGSegmentLegacy(
                    c, nrows, false, 0, trackProvenance));
    } else {
        if (columns.size() != 2) {
            LOG(ERRORL) << "Not supposed to be invoked on non binary predicates";
            throw 10;
        }
        std::vector<std::shared_ptr<Column>> c;
        c.push_back(columns[1]);
        c.push_back(columns[0]);
        return std::shared_ptr<TGSegment>(new TGSegmentLegacy(
                    c, nrows, false, 0, trackProvenance));
    }
}

void TGSegmentLegacy::projectTo(const std::vector<int> &fields,
        std::vector<std::shared_ptr<Column>> &out) const {
    for (auto &field : fields)
        out.push_back(columns[field]);
    if (trackProvenance) {
        out.push_back(columns.back());
    }
}

ProvenanceType TGSegmentLegacy::getProvenanceType() const {
    if (trackProvenance) {
        assert(columns.size() > 0);
        if (columns.back()->isEmpty() || columns.back()->isConstant()) {
            return SAMENODE;
        } else {
            return DIFFNODES;
        }
    } else {
        return NOPROV;
    }
}

size_t TGSegmentLegacy::getNodeId() const {
    if (trackProvenance) {
        assert(columns.size() > 0);
        if (!columns.back()->isEmpty() && columns.back()->isConstant()) {
            return columns.back()->first();
        } else {
            return ~0ul;
        }
    } else {
        return ~0ul;
    }
}

std::shared_ptr<const TGSegment> TGSegmentLegacy::sortByProv() const {
    if (!trackProvenance) {
        LOG(ERRORL) << "This method should not be called if the segment"
            " does not support the provenance";
        throw 10;
    }
    auto nfields = columns.size();
    auto oldcols(columns);
    Segment s(nfields, oldcols);
    std::vector<uint8_t> fields;
    fields.push_back(nfields - 1);
    auto snew = s.sortBy(&fields);
    std::vector<std::shared_ptr<Column>> newColumns;
    for(int i = 0; i < nfields; ++i) {
        newColumns.push_back(snew->getColumn(i));
    }
    return std::shared_ptr<TGSegment>(
            new TGSegmentLegacy(newColumns, s.getNRows(), false, fields[0],
                trackProvenance));
}

size_t TGSegmentLegacy::countHits(const std::vector<Term_t> &terms,
        int column) const {
    return columns[column]->countHits(terms);
}

size_t TGSegmentLegacy::countHits(const std::vector<std::pair<
        Term_t,Term_t>> &terms,
        int column1, int column2) const {
    auto c1 = getColumn(column1);
    auto c2 = getColumn(column2);
    if (c1->isEDB() && c2->isEDB()) {
        EDBColumn *ec1 = (EDBColumn*)c1.get();
        EDBColumn *ec2 = (EDBColumn*)c2.get();
        const Literal &l1 = ec1->getLiteral();
        const Literal &l2 = ec2->getLiteral();
        std::vector<Substitution> subs;
        if (!l1.sameVarSequenceAs(l2) ||
                l1.subsumes(subs, l1, l2) == -1) {
            //The columns come from different literals. This is not supported
            throw 10;
        }
        EDBLayer &layer = ec1->getEDBLayer();
        auto v = layer.checkNewIn(terms, l1, ec1->posColumnInLiteral(),
                ec2->posColumnInLiteral());
        return terms.size() - v.size();
    }
    LOG(ERRORL) << "Not implemented";
    throw 10;
}

TGSegmentLegacy::~TGSegmentLegacy() {
    //std::cout << "Deleting " <<  (void*)this << std::endl;
}
