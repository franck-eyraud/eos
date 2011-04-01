#ifndef __EOSMGM_FSVIEW__HH__
#define __EOSMGM_FSVIEW__HH__

/*----------------------------------------------------------------------------*/
#include "mgm/Namespace.hh"

#include "common/FileSystem.hh"
#include "common/RWMutex.hh"
#include "common/Logging.hh"
#include "common/GlobalConfig.hh"
/*----------------------------------------------------------------------------*/
#include "XrdOuc/XrdOucString.hh"
/*----------------------------------------------------------------------------*/
#ifndef __APPLE__
#include <sys/vfs.h>
#else
#include <sys/param.h>
#include <sys/mount.h>
#endif

#include <map>
#include <set>

#ifndef EOSMGMFSVIEWTEST
#include "mgm/ConfigEngine.hh"
#endif

/*----------------------------------------------------------------------------*/

EOSMGMNAMESPACE_BEGIN

//------------------------------------------------------------------------
//! Classes providing views on filesystems by space,group,node
//------------------------------------------------------------------------

class BaseView : public std::set<eos::common::FileSystem::fsid_t> {
private:
  time_t      mHeartBeat;
  std::string mHeartBeatString;
  std::string mHeartBeatDeltaString;
  std::string mStatus;
  std::string mSize;
public:
  std::string mName;
  std::string mType;
  
  BaseView(){};
  ~BaseView(){};
  
  virtual const char* GetConfigQueuePrefix() { return "";}

  void Print(std::string &out, std::string headerformat, std::string listformat);
  
  virtual std::string GetMember(std::string member);
  virtual bool SetConfigMember(std::string key, string value, bool create=false, std::string broadcastqueue="");
  virtual std::string GetConfigMember(std::string key);

  void SetHeartBeat(time_t hb)       { mHeartBeat = hb;       }
  void SetStatus(const char* status) { mStatus = status;      }
  const char* GetStatus()            { return mStatus.c_str();}
  time_t      GetHeartBeat()         { return mHeartBeat;     }


  long long SumLongLong(const char* param); // calculates the sum of <param> as long long
  double SumDouble(const char* param);      // calculates the sum of <param> as double
  double AverageDouble(const char* param);  // calculates the average of <param> as double
  double SigmaDouble(const char* param);    // calculates the standard deviation of <param> as double
};

class FsSpace : public BaseView {
public:

  FsSpace(const char* name) {mName = name; mType = "spaceview";}
  ~FsSpace() {};

  static std::string gConfigQueuePrefix;
  virtual const char* GetConfigQueuePrefix() { return gConfigQueuePrefix.c_str();}
  static const char* sGetConfigQueuePrefix() { return gConfigQueuePrefix.c_str();}
};

//------------------------------------------------------------------------
class FsGroup : public BaseView {
  friend class FsView;

protected:
  unsigned int mIndex;
  
public:

  FsGroup(const char* name) {mName = name; mType="groupview";}
  ~FsGroup(){};

  unsigned int GetIndex() { return mIndex; }

  static std::string gConfigQueuePrefix;
  virtual const char* GetConfigQueuePrefix() { return gConfigQueuePrefix.c_str();}
  static const char* sGetConfigQueuePrefix() { return gConfigQueuePrefix.c_str();}
};

//------------------------------------------------------------------------
class FsNode : public BaseView {
public:

  FsNode(const char* name) {mName = name; mType="nodesview";}
  ~FsNode(){};

  static std::string gConfigQueuePrefix;
  virtual const char* GetConfigQueuePrefix() { return gConfigQueuePrefix.c_str();}
  static const char* sGetConfigQueuePrefix() { return gConfigQueuePrefix.c_str();}
};

//------------------------------------------------------------------------
class FsView : public eos::common::LogId {
private:
  
  eos::common::FileSystem::fsid_t NextFsId;
  std::map<eos::common::FileSystem::fsid_t , std::string> Fs2UuidMap;
  std::map<std::string, eos::common::FileSystem::fsid_t>  Uuid2FsMap;
  std::string  MgmConfigQueueName;

public:

#ifndef EOSMGMFSVIEWTEST
  static ConfigEngine* ConfEngine;
#endif

  bool Register   (eos::common::FileSystem* fs);  // this adds or modifies a filesystem
  void StoreFsConfig(eos::common::FileSystem* fs);// this stores the filesystem configuration into the config engine and should be called whenever a filesystem wide parameters is changed
  bool UnRegister (eos::common::FileSystem* fs);  // this removes a filesystem
  bool ExistsQueue(std::string queue, std::string queuepath); // check's if a queue+path exists already
  
  bool RegisterNode   (const char* nodequeue);            // this adds or modifies an fst node
  bool UnRegisterNode (const char* nodequeue);            // this removes an fst node

  bool RegisterSpace  (const char* spacename);            // this adds or modifies a space 
  bool UnRegisterSpace(const char* spacename);            // this remove a space

  bool RegisterGroup   (const char* groupname);           // this adds or modifies a group
  bool UnRegisterGroup (const char* groupname);           // this removes a group

  eos::common::RWMutex ViewMutex;  // protecting all xxxView variables
  eos::common::RWMutex MapMutex;   // protecting all xxxMap varables

  std::map<std::string , std::set<FsGroup*> > mSpaceGroupView; // this contains a map from space name => FsGroup (list of fsid's in a subgroup)

  std::map<std::string , FsSpace* > mSpaceView;
  std::map<std::string , FsGroup* > mGroupView;
  std::map<std::string , FsNode* >  mNodeView;

  std::map<eos::common::FileSystem::fsid_t, eos::common::FileSystem*> mIdView;
  std::map<eos::common::FileSystem*, eos::common::FileSystem::fsid_t> mFileSystemView;

  // find filesystem
  eos::common::FileSystem* FindByQueuePath(std::string &queuepath); // this requires that YOU lock the ViewMap beforehand

  // filesystem mapping functions
  eos::common::FileSystem::fsid_t CreateMapping(std::string fsuuid);
  bool                            ProvideMapping(std::string fsuuid, eos::common::FileSystem::fsid_t fsid);
  eos::common::FileSystem::fsid_t GetMapping(std::string fsuuid);
  std::string GetMapping(eos::common::FileSystem::fsid_t fsuuid);
  bool        RemoveMapping(eos::common::FileSystem::fsid_t fsid, std::string fsuuid);
  bool        RemoveMapping(eos::common::FileSystem::fsid_t fsid);

  void PrintSpaces(std::string &out, std::string headerformat, std::string listformat);
  void PrintGroups(std::string &out, std::string headerformat, std::string listformat);
  void PrintNodes (std::string &out, std::string headerformat, std::string listformat);
  
  static std::string GetNodeFormat       (std::string option);
  static std::string GetGroupFormat      (std::string option);
  static std::string GetSpaceFormat      (std::string option);
  static std::string GetFileSystemFormat (std::string option);

  void Reset(); // clears all mappings and filesystem objects

  FsView() { 
    MgmConfigQueueName="";

#ifndef EOSMGMFSVIEWTEST
    ConfEngine = 0;
#endif

  }
  ~FsView() {};

  void SetConfigQueues(const char* mgmconfigqueue, const char* nodeconfigqueue, const char* groupconfigqueue, const char* spaceconfigqueue) {
    FsSpace::gConfigQueuePrefix = spaceconfigqueue;
    FsGroup::gConfigQueuePrefix = groupconfigqueue;
    FsNode::gConfigQueuePrefix  = nodeconfigqueue;
    MgmConfigQueueName = mgmconfigqueue;
  }

#ifndef EOSMGMFSVIEWTEST
  void SetConfigEngine(ConfigEngine* engine) {ConfEngine = engine;}
  bool ApplyFsConfig(const char* key, std::string &val);
  bool ApplyGlobalConfig(const char* key, std::string &val);
#endif

  void SetNextFsId(eos::common::FileSystem::fsid_t fsid);

  static FsView gFsView; // singleton
};

EOSMGMNAMESPACE_END

#endif
