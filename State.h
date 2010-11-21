#ifndef __STATE_H__
#define __STATE_H__
#include "ParallelGravity.h"

// less flexible, but probably more efficient
// and allows for sharing of counters between
// states
class State {
  public:
    int bWalkDonePending; // needed for combiner cache flushes
    // this variable is used instead of TreePiece::currentPrefetch
    // in the prefetch walk
    int currentBucket;  // The bucket we have started to walk.

    // shifted variable into state. there is an issue of redundancy 
    // here, though. in addition to local state, remote and remote-resume
    // state also have this variable but have no use for it, since only
    // a single copy is required.
    // could have made this the third element in the array below
    int myNumParticlesPending;

    // again, redundant variables, since only remote-no-resume
    // walks use this variable to see how many chunks have 
    // been used
    int numPendingChunks;

    // posn 0: bucket requests
    // posn 1: chunk requests
    int *counterArrays[2];
    virtual ~State() {}
};

#if INTERLIST_VER > 0
#if defined CUDA
#include "HostCUDA.h"
#include "DataManager.h"

class DoubleWalkState;

template<typename T>
class GenericList{
  public:
  CkVec<CkVec<T> > lists;
  CkVec<CkVec<int> > offsets;
  int totalNumInteractions;

  GenericList() : totalNumInteractions(0) {}

  void reset(){
    // clear all bucket lists:
    for(int i = 0; i < lists.length(); i++){
      lists[i].length() = 0;
      offsets[i].length() = 0;
    }
    totalNumInteractions = 0;
  }

  void free(){
    for(int i = 0; i < lists.length(); i++){
      lists[i].free();
      offsets[i].free();
    }
    lists.free();
    offsets.free();
    totalNumInteractions = 0;
  }

  void init(int numBuckets, int numper){
    lists.resize(numBuckets);
    offsets.resize(numBuckets);
    for(int i = 0; i < numBuckets; i++){
      lists[i].reserve(numper);
      offsets[i].reserve(numper);
    }
  }

  CudaRequest *serialize(TreePiece *tp);
  void getBucketParameters(TreePiece *tp, 
                           int bucket, 
                           int &bucketStart, int &bucketSize){
                           //std::map<NodeKey, int>&lpref){
	// bucket is listed in this offload
	GenericTreeNode *bucketNode = tp->bucketList[bucket];

	bucketSize = bucketNode->lastParticle - bucketNode->firstParticle + 1;
        bucketStart = bucketNode->bucketArrayIndex;
	CkAssert(bucketStart >= 0);
  }

  void getActiveBucketParameters(TreePiece *tp, 
                           int bucket, 
                           int &bucketStart, int &bucketSize){
                           //std::map<NodeKey, int>&lpref){
	// bucket is listed in this offload
	GenericTreeNode *bucketNode = tp->bucketList[bucket];
        BucketActiveInfo *binfo = &(tp->bucketActiveInfo[bucket]);

	//bucketSize = bucketNode->lastParticle - bucketNode->firstParticle + 1;
        //bucketStart = bucketNode->bucketArrayIndex;
        bucketSize = tp->bucketActiveInfo[bucket].size;
        bucketStart = tp->bucketActiveInfo[bucket].start;
	CkAssert(bucketStart >= 0);
  }

  void push_back(int b, T &ilc, int offset, DoubleWalkState *state, TreePiece *tp);
  

};

#endif

class DoubleWalkState : public State {
  public:
  CheckList *chklists;
  UndecidedLists undlists;
  CkVec<CkVec<OffsetNode> >clists;
  CkVec<CkVec<LocalPartInfo> >lplists;
  CkVec<CkVec<RemotePartInfo> >rplists;
   
  // set once before the first cgr is called for a chunk
  // the idea is to place the chunkRoot (along with replicas)
  // on the remote comp chklist only once per chunk
  //
  // one for each chunk
  bool *placedRoots;
  // to tell a remote-resume state from a remote-no-resume state
  bool resume;

#ifdef CUDA
  int nodeThreshold;
  int partThreshold;

  // two requests for double-buffering

#ifdef CUDA_INSTRUMENT_WRS
  double nodeListTime;
  double partListTime;
#endif

  // during 'small' rungs, buckets are marked when
  // they are included for computation in the request's
  // aux. particle array. these markings should be
  // cleared before the assembly of the next request is
  // begun. for this purpose, we keep track of buckets
  // marked during the construction of a request.
  //
  // NB: for large rungs, we don't mark buckets while 
  // compiling requests. for such rungs, since all
  // particles are shipped at the beginning of the iteration,
  // we have them marked at that time. since all particles,
  // are available on the gpu for these rungs, we do not clear 
  // the markings when requests are sent out.
  CkVec<GenericTreeNode *> markedBuckets;

  // TODO : this switch from map to ckvec means that we cannot 
  // use multiple treepieces per processor, since they will all
  // be writing to the nodeArrayIndex field of the CacheManager's nodes.
  // We need a different group that manages GPU memory for this purpose.
  //std::map<NodeKey,int> nodeMap;
  // CkVec<GenericTreeNode *> nodeMap;
  // std::map<NodeKey,int> partMap;

  // actual lists, one per bucket
  // these include offsets now
  GenericList<CudaMultipoleMoments> moments;
  GenericList<LocalPartInfo> localParticles;
  GenericList<RemotePartInfo> remoteParticles;

  bool nodeOffloadReady(){
    return moments.totalNumInteractions >= nodeThreshold;
  }

  bool localPartOffloadReady(){
    return localParticles.totalNumInteractions >= partThreshold;
  }

  bool remotePartOffloadReady(){
    return remoteParticles.totalNumInteractions >= partThreshold;
  }

#ifdef CUDA_INSTRUMENT_WRS
  void updateNodeThreshold(int t){
    nodeThreshold = t;
  }
  void updatePartThreshold(int t){
    partThreshold = t;
  }
#endif

#endif

  // The lowest nodes reached on paths to each bucket
  // Used to find numBuckets completed when
  // walk returns. Also used to find at which
  // bucket computation should start
  GenericTreeNode *lowestNode;
  int level;

  DoubleWalkState() : chklists(0), lowestNode(0), level(-1)
  {}

#ifdef CUDA_INSTRUMENT_WRS
  void nodeListConstructionTimeStart(){
    nodeListTime = CmiWallTimer();
  }

  double nodeListConstructionTimeStop(){
    return CmiWallTimer()-nodeListTime;
  }

  void partListConstructionTimeStart(){
    partListTime = CmiWallTimer();
  }

  double partListConstructionTimeStop(){
    return CmiWallTimer()-partListTime;
  }

#endif
};
#endif //  INTERLIST_VER 

class NullState : public State {
};

class ListState : public State {
  //public:
  //OffsetNode nodeList;
  //CkVec<LocalPartInfo> localParticleList;
  //CkVec<RemotePartInfo> remoteParticleList;
};

#endif
