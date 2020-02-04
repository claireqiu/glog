#include <vlog/gbruleexecutor.h>
#include <vlog/tgsegmentcache.h>
#include <vlog/tgsegment.h>

std::shared_ptr<const TGSegment> GBRuleExecutor::fromSeg2TGSeg(
        std::shared_ptr<const Segment> seg,
        size_t nodeId, bool isSorted, uint8_t sortedField, bool trackProvenance) {
    auto ncols = seg->getNColumns();
    if (ncols == 1) {
        const std::vector<Term_t> &orig = seg->getColumn(0)->getVectorRef();
        std::vector<Term_t> tuples(orig);
        return std::shared_ptr<const TGSegment>(
                new UnaryTGSegment(tuples, nodeId, isSorted, sortedField));
    } else if (ncols == 2) {
        auto &col1 = seg->getColumn(0)->getVectorRef();
        size_t nrows = col1.size();
        if (trackProvenance) {
            bool constantNodeVal = seg->getColumn(1)->isConstant();
            if (constantNodeVal) {
                std::vector<Term_t> tuples(col1);
                return std::shared_ptr<const TGSegment>(
                        new UnaryWithConstProvTGSegment(
                            tuples, nodeId, isSorted, sortedField));
            } else {
                auto itrProv = seg->getColumn(1)->getReader();
                std::vector<std::pair<Term_t, Term_t>> out(nrows);
                for(size_t i = 0; i < nrows; ++i) {
                    out[i].first = col1[i];
                    if (!itrProv->hasNext()) {
                        throw 10;
                    }
                    out[i].second = itrProv->next();
                }
                return std::shared_ptr<const TGSegment>(
                        new UnaryWithProvTGSegment(out, ~0ul, isSorted, sortedField));
            }
        } else {
            auto &col2 = seg->getColumn(1)->getVectorRef();
            std::vector<std::pair<Term_t, Term_t>> out(nrows);
            for(size_t i = 0; i < nrows; ++i) {
                out[i].first = col1[i];
                out[i].second = col2[i];
            }
            return std::shared_ptr<const TGSegment>(
                    new BinaryTGSegment(out, nodeId, isSorted, sortedField));
        }
    } else if (ncols == 3 && trackProvenance) {
        auto &col1 = seg->getColumn(0)->getVectorRef();
        auto &col2 = seg->getColumn(1)->getVectorRef();
        bool constantNodeVal = seg->getColumn(2)->isConstant();
        size_t nrows = col1.size();
        if (constantNodeVal) {
            std::vector<std::pair<Term_t, Term_t>> out(nrows);
            for(size_t i = 0; i < nrows; ++i) {
                out[i].first = col1[i];
                out[i].second = col2[i];
            }
            return std::shared_ptr<const TGSegment>(
                    new BinaryWithConstProvTGSegment(
                        out, seg->getColumn(2)->first(), isSorted, sortedField));
        } else {
            std::vector<BinWithProv> out(nrows);
            auto itrNode = seg->getColumn(2)->getReader();
            for(size_t i = 0; i < nrows; ++i) {
                out[i].first = col1[i];
                out[i].second = col2[i];
                if (!itrNode->hasNext()) {
                    throw 10;
                }
                out[i].node = itrNode->next();
            }
            return std::shared_ptr<const TGSegment>(
                    new BinaryWithProvTGSegment(
                        out, ~0ul, isSorted, sortedField));
        }
    } else {
        std::vector<std::shared_ptr<Column>> columns;
        for(int i = 0; i < ncols; ++i) {
            columns.push_back(seg->getColumn(i));
        }
        if (trackProvenance) {
            return std::shared_ptr<const TGSegment>(
                    new TGSegmentLegacy(columns, seg->getNRows(), isSorted, sortedField, true));
        } else {
            return std::shared_ptr<const TGSegment>(
                    new TGSegmentLegacy(columns, seg->getNRows(), isSorted, sortedField, false));
        }
    }
}

std::shared_ptr<const TGSegment> GBRuleExecutor::projectHead(const Literal &head,
        std::vector<size_t> &vars,
        std::shared_ptr<const TGSegment> intermediateResults,
        bool shouldSort,
        bool shouldDelDupl) {
    //Project the columns to instantiate the head
    const auto &tupleHead = head.getTuple();
    assert(tupleHead.getSize() == vars.size());
    auto ncolumns = intermediateResults->getNColumns();
    assert(ncolumns == vars.size());
    if (tupleHead.getSize() > 1) {
        if (tupleHead.getSize() == 2) {
            if (vars[0] != tupleHead.get(0).getId()) {
                //I have to swap the variables
                intermediateResults = intermediateResults->swap();
            }
        } else {
            std::vector<int> neworder;
            for(int i = 0; i < tupleHead.getSize(); ++i) {
                auto varId = tupleHead.get(i).getId();
                bool found = false;
                for(int j = 0; j < vars.size(); ++j) {
                    if (vars[j] == varId) {
                        found = true;
                        neworder.push_back(j);
                        break;
                    }
                }
                if (!found) {
                    LOG(ERRORL) << "I could not find a head variable in the body";
                    throw 10;
                }
            }
            intermediateResults = intermediateResults->reorderColumns(neworder);
        }
    }
    if (shouldSort) {
        if (shouldDelDupl) {
            return intermediateResults->sort()->unique();
        } else {
            return intermediateResults->sort();
        }
    } else {
        if (shouldDelDupl) {
            return intermediateResults->unique();
        } else {
            return intermediateResults;
        }
    }
}

std::shared_ptr<const Segment> GBRuleExecutor::postprocessJoin(
        std::shared_ptr<const Segment> &intermediateResults,
        std::vector<std::shared_ptr<Column>> &intermediateResultsNodes) {
    //Take out the last two columns
    auto ncolumns = intermediateResults->getNColumns();
    if (ncolumns < 2) {
        LOG(ERRORL) << "Cannot happen";
        throw 10;
    }
    std::vector<std::shared_ptr<Column>> columns;
    for(int i = 0; i < ncolumns - 2; ++i) {
        columns.push_back(intermediateResults->getColumn(i));
    }
    //For now, always add one extra column
    CompressedColumnBlock b(0, 1, columns[0]->size());
    std::vector<CompressedColumnBlock> blocks;
    blocks.push_back(b);
    columns.push_back(std::shared_ptr<Column>(
                new CompressedColumn(blocks, columns[0]->size())));

    //Save the columns with the mappings to the nodes
    auto col1 = intermediateResults->getColumn(ncolumns - 2);
    auto col2 = intermediateResults->getColumn(ncolumns - 1);
    intermediateResultsNodes.push_back(col1);
    intermediateResultsNodes.push_back(col2);

    //Create new intermediate results
    return std::shared_ptr<const Segment>(new Segment(columns.size(), columns));
}

void GBRuleExecutor::computeVarPos(std::vector<size_t> &leftVars,
        int bodyAtomIdx,
        const std::vector<Literal> &bodyAtoms,
        const Literal &head,
        std::vector<std::pair<int, int>> &joinVarPos,
        std::vector<int> &copyVarPosLeft,
        std::vector<int> &copyVarPosRight) {
    std::set<int> futureOccurrences;
    for(size_t i = bodyAtomIdx + 1; i < bodyAtoms.size(); ++i) {
        auto &lit = bodyAtoms[i];
        auto vars = lit.getAllVars();
        for(auto v : vars)
            futureOccurrences.insert(v);
    }
    auto headVars = head.getAllVars();
    for(auto v : headVars)
        futureOccurrences.insert(v);

    auto &rightBodyAtom = bodyAtoms[bodyAtomIdx];
    auto rightVars = rightBodyAtom.getAllVarsAndPos();
    std::set<int> addedVars;
    if (bodyAtomIdx > 0) {
        //First check whether variables in the left are used in the next body
        //atoms or in the head
        for(size_t i = 0; i < leftVars.size(); ++i) {
            if (futureOccurrences.count(leftVars[i])) {
                copyVarPosLeft.push_back(i);
                addedVars.insert(leftVars[i]);
            }
        }
        //Search for the join variable
        bool found = false;
        for(int i = 0; i < rightVars.size(); ++i) {
            for(int j = 0; j < leftVars.size(); ++j) {
                if (rightVars[i].first == leftVars[j]) {
                    joinVarPos.push_back(std::pair<int,int>(j, rightVars[i].second));
                    found = true;
                    break;
                }
            }
        }
    }

    //Copy variables from the right atom
    for(size_t i = 0; i < rightVars.size(); ++i) {
        if (futureOccurrences.count(rightVars[i].first) &&
                !addedVars.count(rightVars[i].first)) {
            copyVarPosRight.push_back(rightVars[i].second);
            addedVars.insert(rightVars[i].first);
        }
    }
}

std::shared_ptr<const TGSegment> GBRuleExecutor::processFirstAtom_EDB(
        const Literal &atom,
        std::vector<int> &copyVarPos) {
    PredId_t p = atom.getPredicate().getId();
    if (!edbTables.count(p)) {
        auto edbTable = layer.getEDBTable(p);
        edbTables[p] = edbTable;
    }

    if (atom.getTupleSize() == 0) {
        LOG(ERRORL) << "Atoms with arity 0 are not supported";
        throw 10;
    }

    if (atom.hasRepeatedVars()) {
        LOG(ERRORL) << "For the moment we do not handle atoms with repeated vars";
        throw 10;
    }

    //Get the columns
    auto &table = edbTables[p];
    std::vector<std::shared_ptr<Column>> columns;
    if (table->useSegments()) {
        if (copyVarPos.size() != atom.getNVars()) {
            LOG(ERRORL) << "Operation on EDB table not supported";
            throw 10;
        }
        auto seg = table->getSegment();
        for(int i = 0; i < copyVarPos.size(); ++i) {
            int pos = copyVarPos[i];
            auto col = seg->getColumn(pos);
            columns.push_back(col);
        }
    } else {
        //Create a segment with EDBColumn
        auto size = table->getCardinality(atom);
        std::vector<uint8_t> presortPos;
        int copiedVars = 0;
        int varIdx = 0;
        for(size_t i = 0; i < atom.getTupleSize(); ++i) {
            auto term = atom.getTermAtPos(i);
            auto pos = copyVarPos[copiedVars];
            if (i == pos) {
                std::shared_ptr<Column> col;
                if (term.isVariable()) {
                    col = std::shared_ptr<Column>(
                            new EDBColumn(layer, atom, varIdx++, presortPos, false));
                    presortPos.push_back(i);
                } else {
                    col = std::shared_ptr<Column>(
                            new CompressedColumn(term.getValue(), size));
                }
                columns.push_back(col);
                copiedVars++;
                if (copiedVars == copyVarPos.size())
                    break;
            } else if (term.isVariable()) {
                presortPos.push_back(varIdx);
                varIdx++;
            }
        }
    }
    auto nrows = columns[0]->size();
    if (trackProvenance) {
        CompressedColumnBlock b(~0ul, 0, nrows);
        std::vector<CompressedColumnBlock> blocks;
        blocks.push_back(b);
        columns.push_back(std::shared_ptr<Column>(
                    new CompressedColumn(blocks, nrows)));
    }

    return std::shared_ptr<const TGSegment>(
            new TGSegmentLegacy(columns, nrows, true, 0, trackProvenance));
}

std::shared_ptr<const TGSegment> GBRuleExecutor::processFirstAtom_IDB(
        std::shared_ptr<const TGSegment> &input,
        std::vector<int> &copyVarPos,
        size_t nodeId) {
    auto ncols = copyVarPos.size(); //ncol=arity of the relation
    bool project = ncols < input->getNColumns();
    if (copyVarPos.size() == 1) {
        if (project) {
            std::vector<Term_t> tuples;
            input->appendTo(copyVarPos[0], tuples);
            std::sort(tuples.begin(), tuples.end());
            auto itr = std::unique(tuples.begin(), tuples.end());
            tuples.erase(itr, tuples.end());
            if (trackProvenance) {
                return std::shared_ptr<const TGSegment>(new UnaryWithConstProvTGSegment(tuples, nodeId, true, 0));
            } else {
                return std::shared_ptr<const TGSegment>(new UnaryTGSegment(tuples, nodeId, true, 0));
            }
        } else {
            return input; //Can be reused
        }
    } else if (copyVarPos.size() == 2) {
        if (copyVarPos[0] == 0 && copyVarPos[1] == 1) {
            return input; //Can be reused
        } else {
            //Swapped relation
            std::vector<std::pair<Term_t, Term_t>> tuples;
            input->appendTo(1, 0, tuples);
            if (trackProvenance) {
                return std::shared_ptr<const TGSegment>(new BinaryWithConstProvTGSegment(tuples, nodeId, false, 0));
            } else {
                return std::shared_ptr<const TGSegment>(new BinaryTGSegment(tuples, nodeId, false, 0));
            }
        }
    } else {
        LOG(ERRORL) << "Not implemented";
        throw 10;
    }
}

std::shared_ptr<const TGSegment> GBRuleExecutor::mergeNodes(
        std::vector<size_t> &nodeIdxs,
        std::vector<int> &copyVarPos) {
    if (nodeIdxs.size() == 1) {
        size_t idbBodyAtomIdx = nodeIdxs[0];
        auto data  = g.getNodeData(idbBodyAtomIdx);
        return processFirstAtom_IDB(data, copyVarPos, idbBodyAtomIdx);
    } else {
        auto ncols = copyVarPos.size(); //ncol=arity of the relation
        bool shouldSortAndUnique = ncols < g.getNodeData(nodeIdxs[0])->getNColumns() || copyVarPos[0] != 0;
        if (ncols == 1) { //If it has only one column ...
            if (trackProvenance) {
                std::vector<std::pair<Term_t,Term_t>> tuples;
                for(auto idbBodyAtomIdx : nodeIdxs) {
                    g.getNodeData(idbBodyAtomIdx)->appendTo(copyVarPos[0], tuples);
                }
                if (shouldSortAndUnique) {
                    std::sort(tuples.begin(), tuples.end());
                    auto itr = std::unique(tuples.begin(), tuples.end());
                    tuples.erase(itr, tuples.end());
                    return std::shared_ptr<const TGSegment>(new UnaryWithProvTGSegment(tuples, ~0ul, true, 0));
                } else {
                    return std::shared_ptr<const TGSegment>(new UnaryWithProvTGSegment(tuples, ~0ul, false, 0));
                }
            } else {
                std::vector<Term_t> tuples;
                for(auto idbBodyAtomIdx : nodeIdxs)
                    g.getNodeData(idbBodyAtomIdx)->appendTo(copyVarPos[0], tuples);
                if (shouldSortAndUnique) {
                    std::sort(tuples.begin(), tuples.end());
                    auto itr = std::unique(tuples.begin(), tuples.end());
                    tuples.erase(itr, tuples.end());
                    return std::shared_ptr<const TGSegment>(new UnaryTGSegment(tuples, ~0ul, true, 0));
                } else {
                    return std::shared_ptr<const TGSegment>(new UnaryTGSegment(tuples, ~0ul, false, 0));
                }
            }
        } else if (ncols == 2) { //If it has only two columns ...
            if (trackProvenance) {
                std::vector<BinWithProv> tuples;
                for(auto idbBodyAtomIdx : nodeIdxs) {
                    g.getNodeData(idbBodyAtomIdx)->appendTo(copyVarPos[0], copyVarPos[1], tuples);
                }
                if (shouldSortAndUnique) {
                    std::sort(tuples.begin(), tuples.end());
                    auto itr = std::unique(tuples.begin(), tuples.end());
                    tuples.erase(itr, tuples.end());
                    return std::shared_ptr<const TGSegment>(new BinaryWithProvTGSegment(tuples, ~0ul, true, 0));
                } else {
                    return std::shared_ptr<const TGSegment>(new BinaryWithProvTGSegment(tuples, ~0ul, false, 0));
                }
            } else {
                std::vector<std::pair<Term_t,Term_t>> tuples;
                for(auto idbBodyAtomIdx : nodeIdxs) {
                    g.getNodeData(idbBodyAtomIdx)->appendTo(copyVarPos[0], copyVarPos[1], tuples);
                }
                if (shouldSortAndUnique) {
                    std::sort(tuples.begin(), tuples.end());
                    auto itr = std::unique(tuples.begin(), tuples.end());
                    tuples.erase(itr, tuples.end());
                    return std::shared_ptr<const TGSegment>(new BinaryTGSegment(tuples, ~0ul, true, 0));
                } else {
                    return std::shared_ptr<const TGSegment>(new BinaryTGSegment(tuples, ~0ul, false, 0));
                }
            }
        } else {
            LOG(ERRORL) << "Not implemented";
            throw 10;
        }
    }
}

void GBRuleExecutor::nestedloopjoin(
        std::shared_ptr<const TGSegment> inputLeft,
        const std::vector<size_t> &nodesLeft,
        std::shared_ptr<const TGSegment> inputRight,
        const std::vector<size_t> &nodesRight,
        const Literal &literalRight,
        std::vector<std::pair<int, int>> &joinVarsPos,
        std::vector<int> &copyVarPosLeft,
        std::vector<int> &copyVarPosRight,
        std::unique_ptr<SegmentInserter> &output) {

    std::vector<uint8_t> fields1;
    std::vector<uint8_t> fields2;
    for (uint32_t i = 0; i < joinVarsPos.size(); ++i) {
        fields1.push_back(joinVarsPos[i].first);
        fields2.push_back(joinVarsPos[i].second);
    }

    //Sort the left segment by the join variable
    if (!fields1.empty() && !inputLeft->isSortedBy(fields1)) {
        if (nodesLeft.size() > 0) {
            SegmentCache &c = SegmentCache::getInstance();
            if (!c.contains(nodesLeft, fields1)) {
                inputLeft = inputLeft->sortBy(fields1);
                c.insert(nodesLeft, fields1, inputLeft);
            } else {
                inputLeft = c.get(nodesLeft, fields1);
            }
        } else {
            inputLeft = inputLeft->sortBy(fields1);
        }
    }
    std::unique_ptr<TGSegmentDirectItr> itrLeft = inputLeft->directIterator();

    int64_t countLeft = 0;
    std::vector<Term_t> currentKey;
    VTuple t = literalRight.getTuple();
    std::vector<int> positions;
    for(int i = 0; i < t.getSize(); ++i) {
        positions.push_back(i);
    }
    auto sizerow = copyVarPosLeft.size() + copyVarPosRight.size();
    Term_t currentrow[sizerow + 2];

    while (itrLeft->hasNext()) {
        itrLeft->next();
        currentKey.clear();
        itrLeft->mark();
        countLeft = 1;
        for (int i = 0; i < fields1.size(); i++) {
            currentKey.push_back(itrLeft->get(fields1[i]));
        }
        while (itrLeft->hasNext()) {
            itrLeft->next();
            bool equal = true;
            for (int i = 0; i < fields1.size(); i++) {
                auto k = itrLeft->get(fields1[i]);
                if (k != currentKey[i]) {
                    equal = false;
                    break;
                }
            }
            if (!equal) {
                break;
            }
            countLeft++;
        }

        //Do a lookup on the right-side
        for (int i = 0; i < fields2.size(); i++) {
            t.set(VTerm(0,currentKey[i]),fields2[i]);
        }
        Literal l(literalRight.getPredicate(), t);
        auto segRight = processFirstAtom_EDB(l, positions);
        auto itrRight = segRight->iterator();
        while (itrRight->hasNext()) {
            itrRight->next();
            //Materialize the join
            for(int idx = 0; idx < copyVarPosLeft.size(); ++idx) {
                auto leftPos = copyVarPosLeft[idx];
                auto el = itrLeft->get(leftPos);
                currentrow[idx] = el;
            }
            for(int idx = 0; idx < copyVarPosRight.size(); ++idx) {
                auto rightPos = copyVarPosRight[idx];
                auto value = itrRight->get(rightPos);
                currentrow[copyVarPosLeft.size() + idx] = value;
            }
            if (trackProvenance) {
                currentrow[sizerow] = itrLeft->getNodeId();
                currentrow[sizerow + 1] = itrRight->getNodeId();
            }
            output->addRow(currentrow);
        }
    }
}

void GBRuleExecutor::mergejoin(
        std::shared_ptr<const TGSegment> inputLeft,
        const std::vector<size_t> &nodesLeft,
        std::shared_ptr<const TGSegment> inputRight,
        const std::vector<size_t> &nodesRight,
        std::vector<std::pair<int, int>> &joinVarsPos,
        std::vector<int> &copyVarPosLeft,
        std::vector<int> &copyVarPosRight,
        std::unique_ptr<SegmentInserter> &output) {
    std::chrono::system_clock::time_point startL = std::chrono::system_clock::now();

    std::vector<uint8_t> fields1;
    std::vector<uint8_t> fields2;
    for (uint32_t i = 0; i < joinVarsPos.size(); ++i) {
        fields1.push_back(joinVarsPos[i].first);
        fields2.push_back(joinVarsPos[i].second);
    }
    std::chrono::system_clock::time_point startS = std::chrono::system_clock::now();

    //Sort the left segment by the join variable
    if (!fields1.empty() && !inputLeft->isSortedBy(fields1)) {
        if (nodesLeft.size() > 0) {
            SegmentCache &c = SegmentCache::getInstance();
            if (!c.contains(nodesLeft, fields1)) {
                inputLeft = inputLeft->sortBy(fields1);
                c.insert(nodesLeft, fields1, inputLeft);
            } else {
                inputLeft = c.get(nodesLeft, fields1);
            }
        } else {
            inputLeft = inputLeft->sortBy(fields1);
        }
    }
    std::unique_ptr<TGSegmentDirectItr> itrLeft = inputLeft->directIterator();
    //Sort the right segment by the join variable
    if (!fields2.empty() && !inputRight->isSortedBy(fields2)) {
        if (nodesRight.size() > 0) {
            SegmentCache &c = SegmentCache::getInstance();
            if (!c.contains(nodesRight, fields2)) {
                inputRight = inputRight->sortBy(fields2);
                c.insert(nodesRight, fields2, inputRight);
            } else {
                inputRight = c.get(nodesRight, fields2);
            }
        } else {
            inputRight = inputRight->sortBy(fields2);
        }
    }
    std::unique_ptr<TGSegmentItr> itrRight = inputRight->iterator();
    durationMergeSort += std::chrono::system_clock::now() - startS;

    //Do the merge join
    if (itrLeft->hasNext()) {
        itrLeft->next();
    } else {
        return;
    }

    if (itrRight->hasNext()) {
        itrRight->next();
    } else {
        return;
    }

#if DEBUG
    size_t total = 0;
    size_t max = 65536;
#endif

    long countLeft = -1;
    std::vector<Term_t> currentKey;
    auto sizerow = copyVarPosLeft.size() + copyVarPosRight.size();
    Term_t currentrow[sizerow + 2];
    for(int i = 0; i < sizerow + 2; ++i) currentrow[i] = 0;
    int res = TGSegmentItr::cmp(itrLeft.get(), itrRight.get(), joinVarsPos);
    while (true) {
        //Are they matching?
        while (res < 0 && itrLeft->hasNext()) {
            itrLeft->next();
            res = TGSegmentItr::cmp(itrLeft.get(), itrRight.get(), joinVarsPos);
        }

        if (res < 0) //The first iterator is finished
            break;

        while (res > 0 && itrRight->hasNext()) {
            itrRight->next();
            res = TGSegmentItr::cmp(itrLeft.get(), itrRight.get(), joinVarsPos);
        }

        if (res > 0) { //The second iterator is finished
            break;
        }

        if (res == 0) {
            if (countLeft == -1) {
                currentKey.clear();
                itrLeft->mark();
                countLeft = 1;
                for (int i = 0; i < fields1.size(); i++) {
                    currentKey.push_back(itrLeft->get(fields1[i]));
                }
                while (itrLeft->hasNext()) {
                    itrLeft->next();
                    bool equal = true;
                    for (int i = 0; i < fields1.size(); i++) {
                        auto k = itrLeft->get(fields1[i]);
                        if (k != currentKey[i]) {
                            equal = false;
                            break;
                        }
                    }
                    if (! equal) {
                        break;
                    }
                    countLeft++;
                }
            }
#if DEBUG
            total += countLeft;
            while (total >= max) {
                LOG(TRACEL) << "Count = " << countLeft << ", total = " << total;
                max = max + max;
            }
#endif

            itrLeft->reset();
            //Move the left iterator countLeft times and emit tuples
            for(int idx = 0; idx < copyVarPosRight.size(); ++idx) {
                auto rightPos = copyVarPosRight[idx];
                currentrow[copyVarPosLeft.size() + idx] = itrRight->get(rightPos);
            }
            auto c = 0;
            bool leftActive = true;
            while (c < countLeft) {
                for(int idx = 0; idx < copyVarPosLeft.size(); ++idx) {
                    auto leftPos = copyVarPosLeft[idx];
                    auto el = itrLeft->get(leftPos);
                    currentrow[idx] = el;
                }
                if (trackProvenance) {
                    currentrow[sizerow] = itrLeft->getNodeId();
                    currentrow[sizerow + 1] = itrRight->getNodeId();
                }
                output->addRow(currentrow);
                if (itrLeft->hasNext()) {
                    itrLeft->next();
                } else {
                    leftActive = false;
                }
                c++;
            }

            //Move right
            if (!itrRight->hasNext()) {
                break;
            } else {
                itrRight->next();
            }
            //Does the right row have not the same value as before?
            bool equal = true;
            for (int i = 0; i < fields2.size(); i++) {
                auto newKey = itrRight->get(fields2[i]);
                if (newKey != currentKey[i]) {
                    equal = false;
                    break;
                }
            }
            if (! equal) {
                countLeft = -1;
                //LeftItr is already pointing to the next element ...
                if (!leftActive) {
                    break;
                }
                res = TGSegmentItr::cmp(itrLeft.get(), itrRight.get(), joinVarsPos);
            }
        }
    }
#if DEBUG
    LOG(TRACEL) << "Total = " << total;
    std::chrono::duration<double> secL = std::chrono::system_clock::now() - startS;
    LOG(TRACEL) << "merge_join: time : " << secL.count() * 1000;
#endif
}

void GBRuleExecutor::leftjoin(
        std::shared_ptr<const TGSegment> inputLeft,
        std::vector<size_t> &bodyNodeIdxs,
        std::vector<std::pair<int, int>> &joinVarPos,
        std::vector<int> &copyVarPosLeft,
        std::unique_ptr<SegmentInserter> &output) {
    std::shared_ptr<const TGSegment> inputRight;
    LOG(DEBUGL) << "bodyNodeIdxs.size() = " << bodyNodeIdxs.size();
    for (int i = 0; i < bodyNodeIdxs.size(); i++) {
        LOG(DEBUGL) << "bodyNodeIdxs[" << i << "] = " << bodyNodeIdxs[i];
    }
    if (bodyNodeIdxs.size() == 1) {
        size_t idbBodyAtomIdx = bodyNodeIdxs[0];
        inputRight = g.getNodeData(idbBodyAtomIdx);
    } else {
        auto ncols = g.getNodeData(bodyNodeIdxs[0])->getNColumns();
        std::vector<int> projectedPos;
        for(int i = 0; i < ncols; ++i)
            projectedPos.push_back(i);
        inputRight = mergeNodes(bodyNodeIdxs, projectedPos);
    }
    LOG(DEBUGL) << "inputRight cols = " << inputRight->getNColumns() << ", size = " << inputRight->getNRows();

    std::vector<uint8_t> fields1;
    std::vector<uint8_t> fields2;

    for (uint32_t i = 0; i < joinVarPos.size(); ++i) {
        fields1.push_back(joinVarPos[i].first);
        fields2.push_back(joinVarPos[i].second);
    }
    auto sortedInputLeft = inputLeft->sortBy(fields1);
    auto sortedInputRight = inputRight->sortBy(fields2);
    auto itrLeft = sortedInputLeft->iterator();
    auto itrRight = sortedInputRight->iterator();

    bool leftActive = false;
    bool rightActive = false;
    if (itrLeft->hasNext()) {
        itrLeft->next();
        leftActive = true;
    } else {
        return;
    }
    if (itrRight->hasNext()) {
        itrRight->next();
        rightActive = true;
    }

    auto sizerow = copyVarPosLeft.size();
    Term_t currentrow[sizerow + 2];

    while (leftActive && rightActive) {
        int res = TGSegmentItr::cmp(itrLeft.get(), itrRight.get(), joinVarPos);
        if (res <= 0) {
            if (res < 0) {
                for(int idx = 0; idx < copyVarPosLeft.size(); ++idx) {
                    auto leftPos = copyVarPosLeft[idx];
                    auto el = itrLeft->get(leftPos);
                    currentrow[idx] = el;
                }
                output->addRow(currentrow);
            }
            if (itrLeft->hasNext()) {
                itrLeft->next();
            } else {
                leftActive = false;
            }
        } else {
            if (itrRight->hasNext()) {
                itrRight->next();
            } else {
                rightActive = false;
            }
        }
    }
    while (leftActive) {
        for(int idx = 0; idx < copyVarPosLeft.size(); ++idx) {
            auto leftPos = copyVarPosLeft[idx];
            auto el = itrLeft->get(leftPos);
            currentrow[idx] = el;
        }
        output->addRow(currentrow);
        if (itrLeft->hasNext()) {
            itrLeft->next();
        } else {
            leftActive = false;
        }
    }
}

void GBRuleExecutor::join(
        std::shared_ptr<const TGSegment> inputLeft,
        const std::vector<size_t> &nodesLeft,
        std::vector<size_t> &nodesRight,
        const Literal &literalRight,
        std::vector<std::pair<int, int>> &joinVarPos,
        std::vector<int> &copyVarPosLeft,
        std::vector<int> &copyVarPosRight,
        std::unique_ptr<SegmentInserter> &output) {
    std::shared_ptr<const TGSegment> inputRight;
    bool mergeJoinPossible = true;
    if (nodesRight.size() == 1) {
        size_t idbBodyAtomIdx = nodesRight[0];
        inputRight = g.getNodeData(idbBodyAtomIdx);
    } else if (nodesRight.size() > 0) {
        auto ncols = g.getNodeData(nodesRight[0])->getNColumns();
        std::vector<int> projectedPos;
        for(int i = 0; i < ncols; ++i)
            projectedPos.push_back(i);
        inputRight = mergeNodes(nodesRight, projectedPos);
    } else {
        //It must be an EDB literal because only these do not have nodes
        assert(literalRight.getPredicate().getType() == EDB);

        //check if I'm allowed to do a merge join
        if (!layer.isQueryAllowed(literalRight)) {
            mergeJoinPossible = false;
        } else {
            std::vector<int> allVars;
            for(int i = 0; i < literalRight.getTupleSize(); ++i)
                allVars.push_back(i);
            inputRight = processFirstAtom_EDB(literalRight, allVars);
        }
    }

    if (literalRight.isNegated()) {
        // Negated atoms should not introduce new variables.
        assert(copyVarPosRight.size() == 0);
        //TODO: left join should not work with trackProvenance=true
        leftjoin(inputLeft,
                nodesRight,
                joinVarPos,
                copyVarPosLeft,
                output);
    } else {
        if (mergeJoinPossible) {
            mergejoin(inputLeft,
                    nodesLeft,
                    inputRight,
                    nodesRight,
                    joinVarPos,
                    copyVarPosLeft,
                    copyVarPosRight,
                    output);
        } else {
            nestedloopjoin(inputLeft,
                    nodesLeft,
                    inputRight,
                    nodesRight,
                    literalRight,
                    joinVarPos,
                    copyVarPosLeft,
                    copyVarPosRight,
                    output);
        }
    }
}

void GBRuleExecutor::shouldSortDelDupls(const Literal &head,
        const std::vector<Literal> &bodyAtoms,
        const std::vector<std::vector<size_t>> &bodyNodes,
        bool &shouldSort,
        bool &shouldDelDupl) {
    if (bodyAtoms.size() == 1) {
        auto &bodyAtom = bodyAtoms[0];
        auto th = head.getTuple();
        auto tb = bodyAtom.getTuple();
        int sortedFields = 0;
        for(int i = 0; i < th.getSize() && i < tb.getSize(); ++i) {
            if (th.get(i).getId() != tb.get(i).getId()) {
                break;
            }
            sortedFields++;
        }
        shouldSort = !(sortedFields == th.getSize()) || (bodyNodes.size() > 0 && bodyNodes[0].size() > 1);
        shouldDelDupl = th.getSize() < tb.getSize();
    } else {
        shouldSort = shouldDelDupl = true;
    }
}

OutputRule GBRuleExecutor::executeRule(Rule &rule, GBRuleInput &node) {
    auto &bodyNodes = node.incomingEdges;

    //LOG(INFOL) << "Executing rule " << rule.tostring(program, &layer) <<
    //    " " << rule.getFirstHead().getPredicate().getId() << " " << node.ruleIdx;

    //Perform the joins and populate the head
    auto &bodyAtoms = rule.getBody();
    //Maybe Rearrange the body atoms? Don't forget to also re-arrange the body
    //nodes

    std::vector<size_t> varsIntermediate;
    std::shared_ptr<const TGSegment> intermediateResults;
    //The following data structure is used only if trackProvenance=true
    std::vector<std::shared_ptr<Column>> intermediateResultsNodes;
    size_t currentBodyNode = 0;
    bool firstBodyAtomIsIDB = false;

    for(size_t i = 0; i < bodyAtoms.size(); ++i) {
        const Literal &currentBodyAtom = bodyAtoms[i];
        VTuple currentVars = currentBodyAtom.getTuple();

        //Which are the positions (left,right) of the variable to join?
        std::vector<std::pair<int, int>> joinVarPos;
        //Which variables should be copied from the existing collection?
        std::vector<int> copyVarPosLeft;
        //Which variables should be copied from the new body atom?
        std::vector<int> copyVarPosRight;
        //Compute the value of the vars
        computeVarPos(varsIntermediate, i, bodyAtoms, rule.getFirstHead(),
                joinVarPos, copyVarPosLeft, copyVarPosRight);

        //Get the node of the current bodyAtom (if it's IDB)
        std::vector<size_t> newVarsIntermediateResults;
        bool isEDB = currentBodyAtom.getPredicate().getType() == EDB;

        if (i == 0) {
            //Process first body atom, no join
            std::chrono::steady_clock::time_point start =
                std::chrono::steady_clock::now();
            if (isEDB) {
                intermediateResults = processFirstAtom_EDB(currentBodyAtom,
                        copyVarPosRight);
            } else {
                firstBodyAtomIsIDB = true;
                intermediateResults = mergeNodes(
                        bodyNodes[currentBodyNode], copyVarPosRight);
                currentBodyNode++;
            }
            std::chrono::steady_clock::time_point end =
                std::chrono::steady_clock::now();
            durationFirst += end - start;

            //If empty then stop
            if (intermediateResults->isEmpty()) {
                intermediateResults = std::shared_ptr<const TGSegment>();
                break;
            }
        } else {
            const uint8_t extraColumns = trackProvenance ? 2 : 0;
            std::unique_ptr<SegmentInserter> newIntermediateResults
                (new SegmentInserter(copyVarPosLeft.size() +
                                     copyVarPosRight.size() + extraColumns));

            std::chrono::steady_clock::time_point start =
                std::chrono::steady_clock::now();

            //Perform a join (or a left join) between the intermediate results and
            //the new collection
            join(intermediateResults,
                    i == 1 && firstBodyAtomIsIDB ? bodyNodes[0] : noBodyNodes,
                    !isEDB ? bodyNodes[currentBodyNode] : noBodyNodes,
                    currentBodyAtom,
                    joinVarPos,
                    copyVarPosLeft,
                    copyVarPosRight,
                    newIntermediateResults);

            std::chrono::steady_clock::time_point end =
                std::chrono::steady_clock::now();
            durationJoin += end - start;

            //If empty then stop
            if (newIntermediateResults->isEmpty()) {
                intermediateResults = std::shared_ptr<const TGSegment>();
                break;
            }

            std::shared_ptr<const Segment> seg = newIntermediateResults->getSegment();
            if (trackProvenance) {
                //Process the output of nodes
                seg = postprocessJoin(seg, intermediateResultsNodes);
            }
            intermediateResults = fromSeg2TGSeg(seg , ~0ul, false, 0, trackProvenance);
            //Update the list of variables from the left atom
            for(auto varIdx = 0; varIdx < copyVarPosLeft.size(); ++varIdx) {
                auto var = copyVarPosLeft[varIdx];
                newVarsIntermediateResults.push_back(varsIntermediate[var]);
            }
            currentBodyNode++;
        }
        //Update the list of variables from the right atom
        for(auto varIdx = 0; varIdx < copyVarPosRight.size(); ++varIdx) {
            auto varPos = copyVarPosRight[varIdx];
            newVarsIntermediateResults.push_back(
                    currentVars.get(varPos).getId());
        }
        varsIntermediate = newVarsIntermediateResults;
    }

    //Filter out the derivations produced by the rule
    auto nonempty = !(intermediateResults == NULL || intermediateResults->isEmpty());
    if (nonempty) {
        //Compute the head
        std::chrono::steady_clock::time_point start =
            std::chrono::steady_clock::now();
        Literal head = rule.getFirstHead();
        bool shouldSort = true, shouldDelDupl = true;
        shouldSortDelDupls(head, bodyAtoms, bodyNodes, shouldSort, shouldDelDupl);
        intermediateResults = projectHead(head,
                varsIntermediate, intermediateResults,
                shouldSort,
                shouldDelDupl);
        std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now();
        auto dur = end - start;
        durationCreateHead += dur;
        OutputRule o;
        o.first = intermediateResults;
        o.second = intermediateResultsNodes;
        return o;
    } else {
        return OutputRule();
    }
}

void GBRuleExecutor::printStats() {
    LOG(INFOL) << "Time first (ms): " << durationFirst.count();
    LOG(INFOL) << "Time mergesort (ms): " << durationMergeSort.count();
    LOG(INFOL) << "Time joins (ms): " << durationJoin.count();
    LOG(INFOL) << "Time head (ms): " << durationCreateHead.count();
}