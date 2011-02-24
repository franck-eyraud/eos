//          $Id: XrdMqOfs.cc,v 1.00 2007/10/04 01:34:19 ajp Exp $

const char *XrdMqOfsCVSID = "$Id: XrdMqOfs.cc,v 1.0.0 2007/10/04 01:34:19 ajp Exp $";


#include "XrdVersion.hh"
#include "XrdClient/XrdClientAdmin.hh"
#include "XrdOss/XrdOss.hh"
#include "XrdOss/XrdOssApi.hh"
#include "XrdOuc/XrdOucEnv.hh"
#include "XrdOuc/XrdOucTokenizer.hh"
#include "XrdOuc/XrdOucTrace.hh"
#include "XrdOuc/XrdOucTList.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdSys/XrdSysLogger.hh"
#include "XrdSys/XrdSysPthread.hh"
#include "XrdSys/XrdSysTimer.hh"
#include "XrdSec/XrdSecInterface.hh"
#include "XrdSfs/XrdSfsAio.hh"
#include "XrdNet/XrdNetDNS.hh"
#include "XrdMqOfs/XrdMqOfs.hh"
#include "XrdMqOfs/XrdMqMessage.hh"
#include "XrdOfs/XrdOfsTrace.hh"

#include <pwd.h>
#include <grp.h>
#include <signal.h>

/******************************************************************************/
/*                        G l o b a l   O b j e c t s                         */
/******************************************************************************/

XrdSysError     OfsEroute(0);  
extern XrdOssSys *XrdOfsOss;
XrdSysError    *XrdMqOfs::eDest;
extern XrdOucTrace OfsTrace;
extern XrdOss    *XrdOssGetSS(XrdSysLogger *, const char *, const char *);

XrdOucHash<XrdOucString>*     XrdMqOfs::stringstore;

XrdMqOfs   XrdOfsFS;

void
xrdmqofs_shutdown(int sig) {
  
  exit(0);
}

/******************************************************************************/
/*                        C o n v i n i e n c e                               */
/******************************************************************************/

/*----------------------------------------------------------------------------*/
/* this helps to avoid memory leaks by strdup                                 */
/* we maintain a string hash to keep all used user ids/group ids etc.         */

char* 
STRINGSTORE(const char* __charptr__) {
  XrdOucString* yourstring;
  if (!__charptr__ ) return (char*)"";

  if ((yourstring = XrdOfsFS.stringstore->Find(__charptr__))) {
    return (char*)yourstring->c_str();
  } else {
    XrdOucString* newstring = new XrdOucString(__charptr__);
    XrdOfsFS.StoreMutex.Lock();
    XrdOfsFS.stringstore->Add(__charptr__,newstring);
    XrdOfsFS.StoreMutex.UnLock();
    return (char*)newstring->c_str();
  } 
}


XrdMqOfsOutMutex::XrdMqOfsOutMutex() {
  XrdOfsFS.QueueOutMutex.Lock();
}

XrdMqOfsOutMutex::~XrdMqOfsOutMutex() 
{
  XrdOfsFS.QueueOutMutex.UnLock();
}
  
/******************************************************************************/
/*                           C o n s t r u c t o r                            */
/******************************************************************************/

XrdMqOfs::XrdMqOfs(XrdSysError *ep)
{
  eDest = ep;
  ConfigFN  = 0;  
  StartupTime = time(0);
  LastOutputTime = time(0);
  ReceivedMessages = 0;
  FanOutMessages = 0;
  DeliveredMessages = 0;
  AdvisoryMessages = 0;
  UndeliverableMessages = 0;
  DiscardedMonitoringMessages = 0;
  
  (void) signal(SIGINT,xrdmqofs_shutdown);

  fprintf(stderr,"Addr::QueueOutMutex        0x%llx\n",(unsigned long long) &XrdOfsFS.QueueOutMutex);
  fprintf(stderr,"Addr::MessageMutex         0x%llx\n",(unsigned long long) &XrdOfsFS.MessagesMutex);
}

/******************************************************************************/
/*                           I n i t i a l i z a t i o n                      */
/******************************************************************************/
bool
XrdMqOfs::Init (XrdSysError &ep)
{
  stringstore = new XrdOucHash<XrdOucString> ();

  return true;
}


/******************************************************************************/
/*                         G e t F i l e S y s t e m                          */
/******************************************************************************/
  
extern "C" 
XrdSfsFileSystem *XrdSfsGetFileSystem(XrdSfsFileSystem *native_fs, 
                                      XrdSysLogger     *lp,
				      const char       *configfn)
{
  // Do the herald thing
  //
  OfsEroute.SetPrefix("mqofs_");
  OfsEroute.logger(lp);
  OfsEroute.Say("++++++ (c) 2010 CERN/IT-DSS ",
		"v 1.0");

   XrdOfsFS.ConfigFN = (configfn && *configfn ? strdup(configfn) : 0);

   if ( XrdOfsFS.Configure(OfsEroute) ) return 0;
   
   // All done, we can return the callout vector to these routines.
   //
   return &XrdOfsFS;
}
  
/******************************************************************************/
/*                            g e t V e r s i o n                             */
/******************************************************************************/

const char *XrdMqOfs::getVersion() {return XrdVERSION;}

/******************************************************************************/
/*                                 S t a l l                                  */
/******************************************************************************/

int XrdMqOfs::Stall(XrdOucErrInfo   &error, // Error text & code
                  int              stime, // Seconds to stall
                  const char      *msg)   // Message to give
{
  XrdOucString smessage = msg;
  smessage += "; come back in ";
  smessage += stime;
  smessage += " seconds!";
  
  EPNAME("Stall");
  const char *tident = error.getErrUser();
  
  ZTRACE(delay, "Stall " <<stime <<": " << smessage.c_str());

  // Place the error message in the error object and return
  //
  error.setErrInfo(0, smessage.c_str());
  
  // All done
  //
  return stime;
}

int 
XrdMqOfs::stat(const char                *queuename,
	       struct stat               *buf,
	       XrdOucErrInfo             &error,
	       const XrdSecEntity        *client,
	       const char                *opaque) {

  EPNAME("stat");
  const char *tident = error.getErrUser();

  XrdMqMessageOut* Out = 0;

  Statistics();

  ZTRACE(open,"stat by buf: "<< queuename);
  std::string squeue = queuename;

  {
    XrdMqOfsOutMutex qm;
    if ((!XrdOfsFS.QueueOut.count(squeue)) || (!(Out = XrdOfsFS.QueueOut[squeue]))) {
      return XrdMqOfs::Emsg(epname, error, EINVAL,"check queue - no such queue");
    }
    Out->DeletionSem.Wait();
  }

  {
    XrdOfsFS.AdvisoryMessages++;
    // submit an advisory message
    XrdAdvisoryMqMessage amg("AdvisoryQuery", queuename,true, XrdMqMessageHeader::kQueryMessage);
    XrdMqMessageHeader::GetTime(amg.kMessageHeader.kSenderTime_sec,amg.kMessageHeader.kSenderTime_nsec);
    XrdMqMessageHeader::GetTime(amg.kMessageHeader.kBrokerTime_sec,amg.kMessageHeader.kBrokerTime_nsec);
    amg.kMessageHeader.kSenderId = XrdOfsFS.BrokerId;
    amg.Encode();
    //    amg.Print();
    XrdSmartOucEnv* env = new XrdSmartOucEnv(amg.GetMessageBuffer());
    XrdMqOfsMatches matches(XrdOfsFS.QueueAdvisory.c_str(), env, tident, XrdMqMessageHeader::kQueryMessage, queuename);
    XrdMqOfsOutMutex qm;
    if (!XrdOfsFS.Deliver(matches))
      delete env;
  }


  // this should be the case always ...
  ZTRACE(open, "Waiting for message");
  //  Out->MessageSem.Wait(1);
  Out->Lock();
  ZTRACE(open, "Grabbing message");
  
  memset(buf,0,sizeof(struct stat));
  buf->st_blksize= 1024;
  buf->st_dev    = 0;
  buf->st_rdev   = 0;
  buf->st_nlink  = 1;
  buf->st_uid    = 0;
  buf->st_gid    = 0;
  buf->st_size   = Out->RetrieveMessages();
  buf->st_atime  = 0;
  buf->st_mtime  = 0;
  buf->st_ctime  = 0;
  buf->st_blocks = 1024;
  buf->st_ino    = 0;
  buf->st_mode   = S_IXUSR|S_IRUSR|S_IWUSR |S_IFREG;
  Out->UnLock();
  Out->DeletionSem.Post();
  if (buf->st_size == 0) {
    XrdOfsFS.NoMessages++;
  }
  return SFS_OK;
}


int 
XrdMqOfs::stat(const char                *Name,
	       mode_t                    &mode,
	       XrdOucErrInfo             &error,
	       const XrdSecEntity        *client,
	       const char                *opaque) {

  EPNAME("stat");
  const char *tident = error.getErrUser();

  ZTRACE(open,"stat by mode");
  return SFS_ERROR;
}




int
XrdMqOfsFile::open(const char                *queuename,
		   XrdSfsFileOpenMode   openMode,
		   mode_t               createMode,
		   const XrdSecEntity        *client,
		   const char                *opaque)
{
  EPNAME("open");

  ZTRACE(open,"Connecting Queue: " << queuename);
  
  XrdMqOfsOutMutex qm;
  QueueName = queuename;
  std::string squeue = queuename;

  //  printf("%s %s %s\n",QueueName.c_str(),XrdOfsFS.QueuePrefix.c_str(),opaque);
  // check if this queue is accepted by the broker
  if (!QueueName.beginswith(XrdOfsFS.QueuePrefix)) {
    // this queue is not supported by us
    return XrdMqOfs::Emsg(epname, error, EINVAL,"connect queue - the broker does not serve the requested queue");
  }
  

  if (XrdOfsFS.QueueOut.count(squeue)) {
    // this is already open by 'someone'
    return XrdMqOfs::Emsg(epname, error, EBUSY, "connect queue - already connected",queuename);
  }
    
  Out = new XrdMqMessageOut(queuename);

  // check if advisory messages are requested
  XrdOucEnv queueenv((opaque)?opaque:"");

  bool advisorystatus=false;
  bool advisoryquery=false;
  const char* val;
  if ( (val = queueenv.Get(XMQCADVISORYSTATUS))) {
    advisorystatus = atoi(val);
  } 
  if ( (val = queueenv.Get(XMQCADVISORYQUERY))) {
    advisoryquery = atoi(val);
  }

  Out->AdvisoryStatus = advisorystatus;
  Out->AdvisoryQuery  = advisoryquery;

  XrdOfsFS.QueueOut.insert(std::pair<std::string, XrdMqMessageOut*>(squeue, Out));

  ZTRACE(open,"Connected Queue: " << queuename);
  IsOpen = true;

  return SFS_OK;
}

int
XrdMqOfsFile::close() {
  EPNAME("close");

  if (!IsOpen) 
    return SFS_OK;

  ZTRACE(close,"Disconnecting Queue: " << QueueName.c_str());
	 
  std::string squeue = QueueName.c_str();

  {
    XrdMqOfsOutMutex qm; 
    if ((XrdOfsFS.QueueOut.count(squeue)) && (Out = XrdOfsFS.QueueOut[squeue])) {
      // hmm this could create a dead lock
      //      Out->DeletionSem.Wait();
      Out->Lock();
      // we have to take away all pending messages
      Out->RetrieveMessages();
      XrdOfsFS.QueueOut.erase(squeue);
      delete Out;
    }
    Out = 0;
  }

  {
    XrdOfsFS.AdvisoryMessages++;
    // submit an advisory message
    XrdAdvisoryMqMessage amg("AdvisoryStatus", QueueName.c_str(),false, XrdMqMessageHeader::kStatusMessage);
    XrdMqMessageHeader::GetTime(amg.kMessageHeader.kSenderTime_sec,amg.kMessageHeader.kSenderTime_nsec);
    XrdMqMessageHeader::GetTime(amg.kMessageHeader.kBrokerTime_sec,amg.kMessageHeader.kBrokerTime_nsec);
    amg.kMessageHeader.kSenderId = XrdOfsFS.BrokerId;
    amg.Encode();
    //    amg.Print();
    XrdSmartOucEnv* env =new XrdSmartOucEnv(amg.GetMessageBuffer());
    XrdMqOfsMatches matches(XrdOfsFS.QueueAdvisory.c_str(), env, tident, XrdMqMessageHeader::kStatusMessage, QueueName.c_str());
    XrdMqOfsOutMutex qm;
    if (!XrdOfsFS.Deliver(matches))
      delete env;
  }

  return SFS_OK;
}


XrdSfsXferSize 
XrdMqOfsFile::read(XrdSfsFileOffset  fileOffset, 
		    char            *buffer,
		    XrdSfsXferSize   buffer_size) {
  EPNAME("read");
  ZTRACE(open,"read");
  if (Out) {
    unsigned int mlen = Out->MessageBuffer.length();
    ZTRACE(open,"reading size:" << buffer_size);
    if ((unsigned long) buffer_size < mlen) {
      memcpy(buffer,Out->MessageBuffer.c_str(),buffer_size);
      Out->MessageBuffer.erase(0,buffer_size);
      return buffer_size;
    } else {
      memcpy(buffer,Out->MessageBuffer.c_str(),mlen);
      Out->MessageBuffer.clear();
      Out->MessageBuffer.reserve(0);
      return mlen;
    }
  }
  error.setErrInfo(-1, "");
  return SFS_ERROR;
}



int
XrdMqOfsFile::stat(struct stat *buf) {
  EPNAME("stat");
  ZTRACE(open,"fstat");

  if (Out) {
    Out->DeletionSem.Wait();
    // this should be the case always ...
    ZTRACE(open, "Waiting for message");

    {
      XrdOfsFS.AdvisoryMessages++;
      // submit an advisory message
      XrdAdvisoryMqMessage amg("AdvisoryQuery", QueueName.c_str(),true, XrdMqMessageHeader::kQueryMessage);
      XrdMqMessageHeader::GetTime(amg.kMessageHeader.kSenderTime_sec,amg.kMessageHeader.kSenderTime_nsec);
      XrdMqMessageHeader::GetTime(amg.kMessageHeader.kBrokerTime_sec,amg.kMessageHeader.kBrokerTime_nsec);
      amg.kMessageHeader.kSenderId = XrdOfsFS.BrokerId;
      amg.Encode();
      //      amg.Print();
      XrdSmartOucEnv* env = new XrdSmartOucEnv(amg.GetMessageBuffer());
      XrdMqOfsMatches matches(XrdOfsFS.QueueAdvisory.c_str(), env, tident, XrdMqMessageHeader::kQueryMessage, QueueName.c_str());
      XrdMqOfsOutMutex qm;
      if (!XrdOfsFS.Deliver(matches))
	delete env;
    }


    //    Out->MessageSem.Wait(1);
    Out->Lock();
    ZTRACE(open, "Grabbing message");

    memset(buf,0,sizeof(struct stat));
    buf->st_blksize= 1024;
    buf->st_dev    = 0;
    buf->st_rdev   = 0;
    buf->st_nlink  = 1;
    buf->st_uid    = 0;
    buf->st_gid    = 0;
    buf->st_size   = Out->RetrieveMessages();
    buf->st_atime  = 0;
    buf->st_mtime  = 0;
    buf->st_ctime  = 0;
    buf->st_blocks = 1024;
    buf->st_ino    = 0;
    buf->st_mode   = S_IXUSR|S_IRUSR|S_IWUSR |S_IFREG;
    Out->UnLock();
    Out->DeletionSem.Post();

    if (buf->st_size == 0) {
      XrdOfsFS.NoMessages++;
    }
    return SFS_OK;
  }
  ZTRACE(open, "No message queue");
  return SFS_ERROR;
}

/******************************************************************************/
/*                         C o n f i g u r e                                  */
/******************************************************************************/
int XrdMqOfs::Configure(XrdSysError& Eroute)
{
  char *var;
  const char *val;
  int  cfgFD;

  StatisticsFile = "/var/log/xroot/mq/proc/stats";


  QueuePrefix = "/xmessage/";
  QueueAdvisory = "/xmessage/*";

  // extract the manager from the config file
  XrdOucStream Config(&Eroute, getenv("XRDINSTANCE"));

  {
    // borrowed from XrdOfs
    unsigned int myIPaddr = 0;

    char buff[256], *bp;
    int i;

    // Obtain port number we will be using
    //
    myPort = (bp = getenv("XRDPORT")) ? strtol(bp, (char **)0, 10) : 0;

    // Establish our hostname and IPV4 address
    //
    HostName      = XrdNetDNS::getHostName();

    if (!XrdNetDNS::Host2IP(HostName, &myIPaddr)) myIPaddr = 0x7f000001;
    strcpy(buff, "[::"); bp = buff+3;
    bp += XrdNetDNS::IP2String(myIPaddr, 0, bp, 128);
    *bp++ = ']'; *bp++ = ':';
    sprintf(bp, "%d", myPort);
    for (i = 0; HostName[i] && HostName[i] != '.'; i++);
    HostName[i] = '\0';
    HostPref = strdup(HostName);
    HostName[i] = '.';
    Eroute.Say("=====> mq.hostname: ", HostName,"");
    Eroute.Say("=====> mq.hostpref: ", HostPref,"");
    ManagerId=HostName;
    ManagerId+=":";
    ManagerId+=(int)myPort;
    Eroute.Say("=====> mq.managerid: ",ManagerId.c_str(),"");
  }



  if( !ConfigFN || !*ConfigFN) {
    // this error will be reported by XrdOfsFS.Configure
  } else {
    // Try to open the configuration file.
    //
    if ( (cfgFD = open(ConfigFN, O_RDONLY, 0)) < 0)
      return Eroute.Emsg("Config", errno, "open config file fn=", ConfigFN);
    
    Config.Attach(cfgFD);
    // Now start reading records until eof.
    //
    
    while((var = Config.GetMyFirstWord())) {
      if (!strncmp(var, "mq.",3)) {
	var += 3;

	if (!strcmp("queue",var)) {
	  if (( val = Config.GetWord())) {
	    QueuePrefix = val;
	    QueueAdvisory = QueuePrefix;
	    QueueAdvisory += "*";
	  }
	}

	if (!strcmp("statfile",var)) {
	  if (( val = Config.GetWord())) {
	    StatisticsFile = val;
	  }
	}
      }
    }
    
    Config.Close();
  }
 
  XrdOucString basestats = StatisticsFile;
  basestats.erase(basestats.rfind("/"));
  XrdOucString mkdirbasestats="mkdir -p "; mkdirbasestats += basestats; mkdirbasestats += " 2>/dev/null";
  int src =system(mkdirbasestats.c_str());
  if (src)
    fprintf(stderr,"%s returned %d\n", mkdirbasestats.c_str(), src);
  
  BrokerId = "root://";
  BrokerId += ManagerId;
  BrokerId += "/";
  BrokerId += QueuePrefix;

  Eroute.Say("=====> mq.queue: ", QueuePrefix.c_str());
  Eroute.Say("=====> mq.brokerid: ", BrokerId.c_str());
  int rc = XrdOfs::Configure(Eroute);
  return rc;
}

void
XrdMqOfs::Statistics() {
  EPNAME("Statistics");
  StatLock.Lock();
  static bool startup=true;
  static struct timeval tstart;
  static struct timeval tstop;
  static struct timezone tz;
  static long long LastReceivedMessages, LastDeliveredMessages, LastFanOutMessages,LastAdvisoryMessages,LastUndeliverableMessages,LastNoMessages, LastDiscardedMonitoringMessages;
  if (startup) {
    tstart.tv_sec=0;
    tstart.tv_usec=0;
    LastReceivedMessages = LastDeliveredMessages = LastFanOutMessages = LastAdvisoryMessages = LastUndeliverableMessages = LastNoMessages = LastDiscardedMonitoringMessages = 0;
    startup = false;
  }

  gettimeofday(&tstop,&tz);

  if (!tstart.tv_sec) {
    gettimeofday(&tstart,&tz);
    StatLock.UnLock();
    return;
  }

  const char* tident="";
  time_t now = time(0);
  float tdiff = ((tstop.tv_sec - tstart.tv_sec)*1000) + (tstop.tv_usec - tstart.tv_usec)/1000;
  if (tdiff > (10 * 1000) ) {
    // every minute
    XrdOucString tmpfile = StatisticsFile; tmpfile += ".tmp";
    int fd = open(tmpfile.c_str(),O_CREAT|O_RDWR|O_TRUNC, S_IROTH | S_IRGRP | S_IRUSR);
    if (fd >=0) {
      char line[4096];
      int rc=0;
      sprintf(line,"mq.received               %lld\n",ReceivedMessages); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.delivered              %lld\n",DeliveredMessages); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.fanout                 %lld\n",FanOutMessages); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.advisory               %lld\n",AdvisoryMessages); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.undeliverable          %lld\n",UndeliverableMessages); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.droppedmonitoring      %lld\n",DiscardedMonitoringMessages); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.total                  %lld\n",NoMessages); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.queued                 %d\n",(int)Messages.size()); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.nqueues                %d\n",(int)QueueOut.size()); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.backloghits            %lld\n",QueueBacklogHits); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.in_rate                %f\n",(1000.0*(ReceivedMessages-LastReceivedMessages)/(tdiff))); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.out_rate               %f\n",(1000.0*(DeliveredMessages-LastDeliveredMessages)/(tdiff))); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.fan_rate               %f\n",(1000.0*(FanOutMessages-LastFanOutMessages)/(tdiff))); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.advisory_rate          %f\n",(1000.0*(AdvisoryMessages-LastAdvisoryMessages)/(tdiff))); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.undeliverable_rate     %f\n",(1000.0*(UndeliverableMessages-LastUndeliverableMessages)/(tdiff))); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.droppedmonitoring_rate %f\n",(1000.0*(DiscardedMonitoringMessages-LastDiscardedMonitoringMessages)/(tdiff))); rc = write(fd,line,strlen(line));
      sprintf(line,"mq.total_rate             %f\n",(1000.0*(NoMessages-LastNoMessages)/(tdiff))); rc = write(fd,line,strlen(line));
      close(fd);
      ::rename(tmpfile.c_str(),StatisticsFile.c_str());
    }
    gettimeofday(&tstart,&tz);

    ZTRACE(getstats,"*****************************************************");
    ZTRACE(getstats,"Received  Messages            : " << ReceivedMessages);
    ZTRACE(getstats,"Delivered Messages            : " << DeliveredMessages);
    ZTRACE(getstats,"FanOut    Messages            : " << FanOutMessages);
    ZTRACE(getstats,"Advisory  Messages            : " << AdvisoryMessages);
    ZTRACE(getstats,"Undeliverable Messages        : " << UndeliverableMessages);
    ZTRACE(getstats,"Discarded Monitoring Messages : " << DiscardedMonitoringMessages);
    ZTRACE(getstats,"No        Messages            : " << NoMessages);
    ZTRACE(getstats,"Queue     Messages            : " << Messages.size());
    ZTRACE(getstats,"#Queues                       : " << QueueOut.size());
    ZTRACE(getstats,"Deferred  Messages (backlog)  : " << BacklogDeferred);
    ZTRACE(getstats,"Backlog   Messages Hits       : " << QueueBacklogHits);
    char rates[4096];
    sprintf(rates, "Rates: IN: %.02f OUT: %.02f FAN: %.02f ADV: %.02f: UNDEV: %.02f DISCMON: %.02f NOMSG: %.02f" 
	    ,(1000.0*(ReceivedMessages-LastReceivedMessages)/(tdiff))
	    ,(1000.0*(DeliveredMessages-LastDeliveredMessages)/(tdiff))
	    ,(1000.0*(FanOutMessages-LastFanOutMessages)/(tdiff))
	    ,(1000.0*(AdvisoryMessages-LastAdvisoryMessages)/(tdiff))
	    ,(1000.0*(UndeliverableMessages-LastUndeliverableMessages)/(tdiff))
	    ,(1000.0*(DiscardedMonitoringMessages-LastDiscardedMonitoringMessages)/(tdiff))
	    ,(1000.0*(NoMessages-LastNoMessages)/(tdiff)));
    ZTRACE(getstats, rates);
    ZTRACE(getstats,"*****************************************************");
    LastOutputTime = now;
    LastReceivedMessages = ReceivedMessages;
    LastDeliveredMessages = DeliveredMessages;
    LastFanOutMessages = FanOutMessages;
    LastAdvisoryMessages = AdvisoryMessages;
    LastUndeliverableMessages = UndeliverableMessages;
    LastNoMessages = NoMessages;
    LastDiscardedMonitoringMessages = DiscardedMonitoringMessages;

  }

  StatLock.UnLock();
}
