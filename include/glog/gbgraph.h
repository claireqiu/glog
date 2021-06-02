#ifndef _GB_GRAPH_H
#define _GB_GRAPH_H

#include <vlog/concepts.h>
#include <vlog/chasemgmt.h>

#include <glog/gbsegment.h>

#include <map>

class GBQuerier;
class GBSegmentInserter;
class GBGraph {
    public:
        typedef enum ProvenanceType {
            NOPROV, //no provenance
            NODEPROV, //we know the nodes used to produce the derivations
            FULLPROV //for each fact, we know the facts that generated it
        } ProvenanceType;

    private:
        struct GBGraph_Node {
            private:
                std::shared_ptr<const TGSegment> data;

                bool queryCreated;
                std::unique_ptr<Literal> queryHead; //Used only for query containment
                std::vector<Literal> queryBody; //Used only for query containment
                std::vector<size_t> rangeQueryBody; //Record the boundaries
                //between the rewritings
                std::vector<size_t> incomingEdges;

                void createQueryFromNode(GBGraph &g);

            public:
                PredId_t predid;
                size_t ruleIdx;
                size_t step;

                GBGraph_Node() : queryCreated(false), ruleIdx(0), step(0) {}

                std::shared_ptr<const TGSegment> getData() const {
                    return data;
                }

                static std::unique_ptr<Literal> createQueryFromNode(
                        uint32_t &outputCounter,
                        std::vector<Literal> &outputQueryBody,
                        std::vector<size_t> &rangeOutputQueryBody,
                        const Rule &rule,
                        const std::vector<size_t> &incomingEdges,
                        GBGraph &g);

                void setData(std::shared_ptr<const TGSegment> data) {
                    this->data = data;
                }

                const Literal &getQueryHead(GBGraph &g);

                const std::vector<Literal> &getQueryBody(GBGraph &g);

                //the flag check is added for debugging purposes. Can be removed
                const std::vector<size_t> &getIncomingEdges(bool check=true) const;

                void setIncomingEdges(const std::vector<size_t> &i) {
                    incomingEdges = i;
                }
        };

        struct GBGraph_TmpPredNode {
            std::shared_ptr<const TGSegment> data;
            std::vector<std::shared_ptr<Column>> nodes;
            size_t ruleIdx;
            size_t step;
        };

        struct CacheRetainEntry {
            size_t nnodes;
            std::shared_ptr<const TGSegment> seg;
        };

        const ProvenanceType provenanceType;
        const bool cacheRetainEnabled;
        const bool queryContEnabled;
        const bool duplAllowed;

        std::map<PredId_t, std::vector<size_t>> pred2Nodes;
        std::vector<GBGraph_Node> nodes;
        std::vector<size_t> noNodes;

        std::map<uint64_t, GBGraph_Node> mapTmpNodes;
        std::map<PredId_t, std::vector<GBGraph_TmpPredNode>> mapPredTmpNodes;
        std::map<PredId_t, CacheRetainEntry> cacheRetain;

        //Counter variables
        uint64_t counterNullValues;
        uint32_t counterFreshVarsQueryCont;
        uint64_t counterTmpNodes;
        uint64_t startCounterTmpNodes;

        Rule *allRules;
        EDBLayer *layer;
        Program *program;

        std::chrono::duration<double, std::milli> durationRetain;
        std::chrono::duration<double, std::milli> durationQueryContain;
        std::chrono::duration<double, std::milli> durationQueryContain1;
        std::chrono::duration<double, std::milli> durationQueryContain2;
        std::chrono::duration<double, std::milli> durationEDBCheck;

        std::shared_ptr<const TGSegment> retainVsNodeFast(
                std::shared_ptr<const TGSegment> existuples,
                std::shared_ptr<const TGSegment> newtuples,
                std::vector<std::shared_ptr<Column>> &derivationNodes);

        std::shared_ptr<const TGSegment> retainVsNodeFast_one(
                std::shared_ptr<const TGSegment> existuples,
                std::shared_ptr<const TGSegment> newtuples,
                std::vector<std::shared_ptr<Column>> &derivationNodes);

        std::shared_ptr<const TGSegment> retainVsNodeFast_two(
                std::shared_ptr<const TGSegment> existuples,
                std::shared_ptr<const TGSegment> newtuples,
                std::vector<std::shared_ptr<Column>> &derivationNodes);

        std::shared_ptr<const TGSegment> retainVsNodeFast_generic(
                std::shared_ptr<const TGSegment> existuples,
                std::shared_ptr<const TGSegment> newtuples,
                std::vector<std::shared_ptr<Column>> &derivationNodes);

        /*** Implemented in gbgraph_redundant.cpp ***/
        bool isRedundant_checkTypeAtoms(const std::vector<Literal> &atoms);

        bool isRedundant_checkEquivalenceEDBAtoms(
                bool &retainFree,
                std::vector<size_t> &bodyNodeIdxs,
                const Literal &originalRuleHead,
                const std::vector<Literal> &originalRuleBody,
                const Literal *rewrittenRuleHead,
                const std::vector<Literal> &rewrittenRuleBody,
                const std::vector<size_t> &rangeRewrittenRuleBody,
                const size_t nodeId);

        bool isRedundant_checkEquivalenceEDBAtoms_one(
                bool &retainFree,
                std::vector<size_t> &bodyNodeIdxs,
                const Literal &originalRuleHead,
                const std::vector<Literal> &originalRuleBody,
                const Literal *rewrittenRuleHead,
                const std::vector<Literal> &rewrittenRuleBody,
                const std::vector<size_t> &rangeRewrittenRuleBody,
                const size_t nodeId);

        void isRedundant_checkEquivalenceEDBAtoms_one_mem_mem(
                std::vector<Term_t> &out,
                std::shared_ptr<const TGSegment> newSeg,
                int posNew,
                std::shared_ptr<const TGSegment> oldSeg,
                int posOld,
                bool stopAfterFirst);

        void isRedundant_checkEquivalenceEDBAtoms_one_mem_mem(
                std::unique_ptr<GBSegmentInserter> &out,
                std::shared_ptr<const TGSegment> newSeg,
                int posNew,
                std::shared_ptr<const TGSegment> oldSeg,
                int posOld,
                bool stopAfterFirst);

        void isRedundant_checkEquivalenceEDBAtoms_one_edb_mem(
                std::vector<Term_t> &out,
                std::shared_ptr<const TGSegment> newSeg,
                int posNew,
                std::shared_ptr<const TGSegment> oldSeg,
                int posOld,
                bool stopAfterFirst);

        void isRedundant_checkEquivalenceEDBAtoms_one_mem_edb(
                std::vector<Term_t> &out,
                std::shared_ptr<const TGSegment> newSeg,
                int posNew,
                std::shared_ptr<const TGSegment> oldSeg,
                int posOld,
                bool stopAfterFirst);

        void isRedundant_checkEquivalenceEDBAtoms_one_edb_edb(
                std::vector<Term_t> &out,
                std::shared_ptr<const TGSegment> newSeg,
                int posNew,
                std::shared_ptr<const TGSegment> oldSeg,
                int posOld,
                bool stopAfterFirst);

        bool isRedundant_checkEquivalenceEDBAtoms_two(
                bool &retainFree,
                std::vector<size_t> &bodyNodeIdxs,
                const Literal &originalRuleHead,
                const std::vector<Literal> &originalRuleBody,
                const Literal *rewrittenRuleHead,
                const std::vector<Literal> &rewrittenRuleBody,
                const std::vector<size_t> &rangeRewrittenRuleBody,
                const size_t nodeId);

        void isRedundant_checkEquivalenceEDBAtoms_two_mem_mem(
                std::vector<std::pair<Term_t,Term_t>> &out,
                std::shared_ptr<const TGSegment> newSeg,
                int posNew1,
                int posNew2,
                std::shared_ptr<const TGSegment> oldSeg,
                int posOld1,
                int posOld2);

        void isRedundant_checkEquivalenceEDBAtoms_two_edb_edb(
                std::vector<std::pair<Term_t,Term_t>> &out,
                std::shared_ptr<const TGSegment> newSeg,
                int posNew1,
                int posNew2,
                std::shared_ptr<const TGSegment> oldSeg,
                int posOld1,
                int posOld2);

        void isRedundant_checkEquivalenceEDBAtoms_two_edb_mem(
                std::vector<std::pair<Term_t,Term_t>> &out,
                std::shared_ptr<const TGSegment> newSeg,
                int posNew1,
                int posNew2,
                std::shared_ptr<const TGSegment> oldSeg,
                int posOld1,
                int posOld2);

        void isRedundant_checkEquivalenceEDBAtoms_two_mem_edb(
                std::vector<std::pair<Term_t,Term_t>> &out,
                std::shared_ptr<const TGSegment> newSeg,
                int posNew1,
                int posNew2,
                std::shared_ptr<const TGSegment> oldSeg,
                int posOld1,
                int posOld2);

        /*** END Implemented in gbgraph_redundant.cpp ***/

        SegProvenanceType getSegProvenanceType(bool multipleNodes = true) const;

        void addNode(PredId_t predId,
                size_t ruleIdx,
                size_t step,
                std::shared_ptr<const TGSegment> data,
                const std::vector<size_t> &incomingEdges,
                std::unique_ptr<Literal> outputQueryHead,
                std::vector<Literal> &outputQueryBody);

        uint64_t addTmpNode(PredId_t predId,
                std::shared_ptr<const TGSegment> data);

        bool shouldTrackProvenance() const {
            return provenanceType != NOPROV;
        }

        const GBGraph_Node &getNode(size_t nodeId) const {
            if (provenanceType != ProvenanceType::NOPROV &&
                    nodeId >= startCounterTmpNodes) {
                assert(mapTmpNodes.count(nodeId));
                return mapTmpNodes.find(nodeId)->second;
            } else {
                assert(nodeId < nodes.size());
                return nodes[nodeId];
            }
        }

        const Literal &getNodeHeadQuery(size_t nodeId) {
            GBGraph_Node &n = nodes[nodeId];
            return n.getQueryHead(*this);
        }

        const std::vector<Literal> &getNodeBodyQuery(size_t nodeId) {
            GBGraph_Node &n = nodes[nodeId];
            return n.getQueryBody(*this);
        }

        std::shared_ptr<const TGSegment> retainAndAddFromTmpNodes_rewriteNode(
                std::shared_ptr<const TGSegment> toBeRewrittenNode,
                const GBGraph_TmpPredNode &node);

        std::shared_ptr<const TGSegment> mergeNodes_special_unary1(
                std::shared_ptr<const TGSegment> seg,
                const std::vector<size_t> &nodeIdxs,
                const std::vector<int> &copyVarPos,
                bool lazyMode,
                bool replaceOffsets,
                bool removeDuplicates) const;

        std::shared_ptr<const TGSegment> mergeNodes_special_unary2(
                const std::vector<size_t> &nodeIdxs,
                const std::vector<int> &copyVarPos,
                bool lazyMode,
                bool replaceOffsets,
                bool removeDuplicates) const;

        std::shared_ptr<const TGSegment> mergeNodes_general(
                const std::vector<size_t> &nodeIdxs,
                const std::vector<Term_t> &filterConstants,
                const std::vector<int> &copyVarPos,
                bool lazyMode,
                bool replaceOffsets,
                bool shouldSort,
                bool removeDuplicates) const;

        std::shared_ptr<const TGSegment> retain(
                PredId_t pred,
                std::shared_ptr<const TGSegment> newtuples) {
            std::vector<std::shared_ptr<Column>> derivationNodes;
            return retain(pred, newtuples, derivationNodes);
        }

        std::shared_ptr<const TGSegment> retainFromDerivationTree(
                PredId_t p,
                size_t ruleIdx,
                std::shared_ptr<const TGSegment> newtuples,
                std::vector<size_t> derivationNodes);

        void retainFromDerivationTree_getNodes(size_t nodeId,
                size_t offsetNodeId,
                PredId_t predId,
                std::vector<std::pair<size_t,size_t>> currentPath,
                std::vector<std::vector<std::pair<size_t,size_t>>> &out);

    public:
        GBGraph(ProvenanceType provenanceType,
                bool cacheRetainEnabled,
                bool useQueryContainmentForRedundancyElim = false,
                bool duplAllowed = false) :
            provenanceType(provenanceType),
            cacheRetainEnabled(cacheRetainEnabled),
            queryContEnabled(useQueryContainmentForRedundancyElim),
            duplAllowed(duplAllowed),
            durationRetain(0),
            durationQueryContain(0),
            durationQueryContain1(0),
            durationQueryContain2(0),
            durationEDBCheck(0),
            /*durationDebug(0),*/
            allRules(NULL),
            layer(NULL), program(NULL) {
                counterNullValues = RULE_SHIFT(1);
                counterFreshVarsQueryCont = 1 << 20;
                counterTmpNodes = 1ul << 40;
                startCounterTmpNodes = counterTmpNodes;
            }

        size_t getNNodes() const {
            return nodes.size();
        }

        size_t getNFacts() const;

        size_t getNEdges() const;

        bool isTmpNode(size_t nodeId) const {
            return nodeId >= startCounterTmpNodes;
        }

        size_t getNodeStep(size_t nodeId) const {
            return getNode(nodeId).step;
        }

        ProvenanceType getProvenanceType() const {
            return provenanceType;
        }

        size_t getNodeRuleIdx(size_t nodeId) const {
            return getNode(nodeId).ruleIdx;
        }

        const std::vector<size_t> &getNodeIncomingEdges(size_t nodeId) const {
            return getNode(nodeId).getIncomingEdges();
        }

        void setRulesProgramLayer(Rule *allRules,
                Program *program,
                EDBLayer *layer) {
            this->allRules = allRules;
            this->program = program;
            this->layer = layer;
        }

        size_t getNodeSize(size_t nodeId) const {
            return getNode(nodeId).getData()->getNRows();
        }

        std::shared_ptr<const TGSegment> getNodeData(size_t nodeId) const {
            return getNode(nodeId).getData();
        }

        std::unique_ptr<TGSegmentItr> getNodeIterator(size_t nodeId) const {
            return getNodeData(nodeId)->iterator();
        }

        PredId_t getNodePredicate(size_t nodeId) const {
            return getNode(nodeId).predid;
        }

        bool areNodesWithPredicate(PredId_t predId) const {
            return pred2Nodes.count(predId);
        }

        const std::vector<size_t> &getNodeIDsWithPredicate(
                PredId_t predId) const {
            if (pred2Nodes.count(predId)) {
                return pred2Nodes.at(predId);
            } else {
                return noNodes;
            }
        }

        const std::vector<PredId_t> getPredicateIDs() const;

        std::shared_ptr<const TGSegment> mergeNodes(
                const std::vector<size_t> &nodeIdxs,
                const std::vector<Term_t> &filterConstants,
                const std::vector<int> &copyVarPos,
                bool lazyMode = false,
                bool replaceOffsets = false,
                bool removeDuplicates = true) const;

        std::shared_ptr<const TGSegment> mergeNodes(
                const std::vector<size_t> &nodeIdxs,
                const std::vector<int> &copyVarPos,
                bool lazyMode = false,
                bool replaceOffsets = false) const {
            std::vector<Term_t> filterConstants;
            return mergeNodes(nodeIdxs, filterConstants, copyVarPos,
                    lazyMode, replaceOffsets);
        }

        void addNodeNoProv(PredId_t predId,
                size_t ruleIdx,
                size_t step,
                std::shared_ptr<const TGSegment> data);

        void addNodeProv(PredId_t predId,
                size_t ruleIdx,
                size_t step,
                std::shared_ptr<const TGSegment> data,
                const std::vector<size_t> &incomingEdges);

        void addNodesProv(PredId_t predId,
                size_t ruleIdx,
                size_t step,
                std::shared_ptr<const TGSegment> data,
                const std::vector<std::shared_ptr<Column>> &provenance,
                bool filterFromProvenance = false);

        void addNodeToBeRetained(PredId_t predId,
                std::shared_ptr<const TGSegment> data,
                std::vector<std::shared_ptr<Column>> &nodes,
                size_t ruleIdx,
                size_t step);

        void addNode(PredId_t predId,
                size_t step,
                std::vector<std::vector<std::string>> &facts);

        void replaceEqualTerms(
                size_t ruleIdx,
                size_t step,
                std::shared_ptr<const TGSegment> data);

        size_t getNDerivedFacts() const {
            size_t nderived = 0;
            for(auto &node : nodes) {
                nderived += node.getData()->getNRows();
            }
            return nderived;
        }

        uint64_t getCounterNullValues() const {
            return counterNullValues;
        }

        void setCounterNullValues(uint64_t c) {
            counterNullValues = c;
        }

        void cleanTmpNodes() {
            mapTmpNodes.clear();
            mapPredTmpNodes.clear();
        }

        std::shared_ptr<const TGSegment> retain(
                PredId_t pred,
                std::shared_ptr<const TGSegment> newtuples,
                std::vector<std::shared_ptr<Column>> &derivationNodes,
                bool inputWithDuplicates = false);

        //Returns the number of retained tuples. The new node will get the last
        //step and will be assigned to rule ~0ul
        uint64_t mergeNodesWithPredicateIntoOne(PredId_t predId);

        //Some predicates are not retained right after each rule
        void retainAndAddFromTmpNodes(PredId_t predId);

        bool isRedundant(size_t ruleIdx,
                std::vector<size_t> &bodyNodeIdx,
                bool edbCheck,
                bool &retainFree);

        std::shared_ptr<GBQuerier> getQuerier() const;

        void printStats() {
            LOG(INFOL) << "Time retain (ms): " << durationRetain.count();
            LOG(INFOL) << "Time query containment (unary) (ms): " <<
                durationQueryContain1.count();
            LOG(INFOL) << "Time query containment (binary) (ms): " <<
                durationQueryContain2.count();
            LOG(INFOL) << "Time query containment (total) "
                "(includes EDB check) (ms): " <<
                durationQueryContain.count();
            LOG(INFOL) << "Time EDB check (ms): " <<
                durationEDBCheck.count();
            //LOG(INFOL) << "(GBGraph) Time debug (ms): " << durationDebug.count();
        }
};

#endif
