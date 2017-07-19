// ----------------------------------------------------------------------
// File: Find.cc
// Author: Andreas-Joachim Peters - CERN
// ----------------------------------------------------------------------

/************************************************************************
 * EOS - the CERN Disk Storage System                                   *
 * Copyright (C) 2011 CERN/Switzerland                                  *
 *                                                                      *
 * This program is free software: you can redistribute it and/or modify *
 * it under the terms of the GNU General Public License as published by *
 * the Free Software Foundation, either version 3 of the License, or    *
 * (at your option) any later version.                                  *
 *                                                                      *
 * This program is distributed in the hope that it will be useful,      *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of       *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        *
 * GNU General Public License for more details.                         *
 *                                                                      *
 * You should have received a copy of the GNU General Public License    *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.*
 ************************************************************************/


// -----------------------------------------------------------------------
// This file is included source code in XrdMgmOfs.cc to make the code more
// transparent without slowing down the compilation time.
// -----------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
int
XrdMgmOfs::_find (const char *path,
                  XrdOucErrInfo &out_error,
                  XrdOucString &stdErr,
                  eos::common::Mapping::VirtualIdentity &vid,
                  std::map<std::string, std::set<std::string> > &found,
                  const char* key,
                  const char* val,
                  bool nofiles,
                  time_t millisleep,
                  bool nscounter, 
		  int maxdepth, 
		  const char* filematch
                  )
/*----------------------------------------------------------------------------*/
/*
 * @brief low-level namespace find command
 *
 * @param path path to start the sub-tree find
 * @param stdErr stderr output string
 * @param vid virtual identity of the client
 * @param found result map/set of the find
 * @param key search for a certain key in the extended attributes
 * @param val search for a certain value in the extended attributes (requires key)
 * @param nofiles if true returns only directories, otherwise files and directories
 * @param millisleep milli seconds to sleep between each directory scan
 * @param maxdepth is the maximum search depth
 * @param filematch is a pattern match for file names

 * The find command distinuishes 'power' and 'normal' users. If the virtual
 * identity indicates the root or admin user queries are unlimited.
 * For others queries are by dfeault limited to 50k directories and 100k files and an
 * appropriate error/warning message is written to stdErr. 
 * Find limits can be (re-)defined in the access interface by using global rules
 * => access set limit 100000 rate:user:*:FindFiles
 * => access set limit 50000 rate:user:*:FindDirs
 * or individual rules
 * => access set limit 100000000 rate:user:eosprod:FindFiles
 * => access set limit 100000000 rate:user:eosprod:FindDirs
 * If 'key' contains a wildcard character in the end find produces a list of
 * directories containing an attribute starting with that key match like
 * var=sys.policy.*
 * The millisleep variable allows to slow down full scans to decrease impact
 * when doing large scans.
 *
 */
/*----------------------------------------------------------------------------*/
{
  std::vector< std::vector<std::string> > found_dirs;

  // try if that is directory
  eos::ContainerMD* cmd = 0;
  std::string Path = path;
  XrdOucString sPath = path;
  errno = 0;
  XrdSysTimer snooze;

  EXEC_TIMING_BEGIN("Find");

  if (nscounter)
  {
    gOFS->MgmStats.Add("Find", vid.uid, vid.gid, 1);
  }

  if (!(sPath.endswith('/')))
    Path += "/";

  found_dirs.resize(1);
  found_dirs[0].resize(1);
  found_dirs[0][0] = Path.c_str();
  int deepness = 0;

  // users cannot return more than 100k files and 50k dirs with one find, 
  // unless there is an access rule allowing deeper queries

  static unsigned long long finddiruserlimit = 50000;
  static unsigned long long findfileuserlimit = 100000;

  {
    // see if there are special access settings
    eos::common::RWMutexReadLock lock(Access::gAccessMutex);
    if (Access::gStallUserGroup)
    {
      std::string usermatchfiles = "rate:user:";
      usermatchfiles += vid.uid_string;
      usermatchfiles += ":";
      usermatchfiles += "FindFiles";
      std::string groupmatchfiles = "rate:group:";
      groupmatchfiles += vid.gid_string;
      groupmatchfiles += ":";
      groupmatchfiles += "FindFiles";
      std::string userwildcardmatchfiles = "rate:user:*:FindFiles";
      
      if (Access::gStallRules.count(usermatchfiles))
      {
	findfileuserlimit = strtoul(Access::gStallRules[usermatchfiles].c_str(), 0, 10);
      }
      else if (Access::gStallRules.count(groupmatchfiles))
      {
	findfileuserlimit = strtoul(Access::gStallRules[groupmatchfiles].c_str(), 0, 10);
      }
      else if (Access::gStallRules.count(userwildcardmatchfiles))
      {
	findfileuserlimit = strtoul(Access::gStallRules[userwildcardmatchfiles].c_str(), 0, 10);
      }

      std::string usermatchdirs = "rate:user:";
      usermatchdirs += vid.uid_string;
      usermatchdirs += ":";
      usermatchdirs += "FindDirs";
      std::string groupmatchdirs = "rate:group:";
      groupmatchdirs += vid.gid_string;
      groupmatchdirs += ":";
      groupmatchdirs += "FindDirs";
      std::string userwildcardmatchdirs = "rate:user:*:FindDirs";
      
      if (Access::gStallRules.count(usermatchdirs))
      {
	finddiruserlimit = strtoul(Access::gStallRules[usermatchdirs].c_str(), 0, 10);
      }
      else if (Access::gStallRules.count(groupmatchdirs))
      {
	finddiruserlimit = strtoul(Access::gStallRules[groupmatchdirs].c_str(), 0, 10);
      }
      else if (Access::gStallRules.count(userwildcardmatchdirs))
      {
	finddiruserlimit = strtoul(Access::gStallRules[userwildcardmatchdirs].c_str(), 0, 10);
      }
    }
  }


  unsigned long long filesfound = 0;
  unsigned long long dirsfound = 0;

  bool limitresult = false;
  bool limited = false;

  if ((vid.uid != 0) && (!eos::common::Mapping::HasUid(3, vid.uid_list)) &&
      (!eos::common::Mapping::HasGid(4, vid.gid_list)) && (!vid.sudoer))
  {
    limitresult = true;
  }

  do
  {
    bool permok = false;

    found_dirs.resize(deepness + 2);
    // loop over all directories in that deepness
    for (unsigned int i = 0; i < found_dirs[deepness].size(); i++)
    {
      Path = found_dirs[deepness][i].c_str();
      eos_static_debug("Listing files in directory %s", Path.c_str());

      if (millisleep)
      {
        // slow down the find command without having locks
        snooze.Wait(millisleep);
      }
      // -----------------------------------------------------------------------
      eos::common::RWMutexReadLock lock(gOFS->eosViewRWMutex);
      try
      {
        cmd = gOFS->eosView->getContainer(Path.c_str(), false);
        permok = cmd->access(vid.uid, vid.gid, R_OK | X_OK);
      }
      catch (eos::MDException &e)
      {
        errno = e.getErrno();
        cmd = 0;
        eos_debug("msg=\"exception\" ec=%d emsg=\"%s\"\n",
                  e.getErrno(), e.getMessage().str().c_str());
      }

      if (cmd)
      {
	if (!permok)
	{
	  // check-out for ACLs
	  permok = _access(Path.c_str(), R_OK|X_OK, out_error, vid, "")?false:true;
	}
        if (!permok)
        {
          stdErr += "error: no permissions to read directory ";
          stdErr += Path.c_str();
          stdErr += "\n";
          continue;
        }

        // add all children into the 2D vectors
        eos::ContainerMD::ContainerMap::iterator dit;
        for (dit = cmd->containersBegin(); dit != cmd->containersEnd(); ++dit)
        {
          std::string fpath = Path.c_str();
          fpath += dit->second->getName();
          fpath += "/";
          // check if we select by tag
          if (key)
          {
            XrdOucString wkey = key;
            if (wkey.find("*") != STR_NPOS)
            {
              // this is a search for 'beginswith' match
              eos::ContainerMD::XAttrMap attrmap;
              if (!gOFS->_attr_ls(fpath.c_str(),
                                  out_error,
                                  vid,
                                  (const char*) 0,
                                  attrmap,
				  false))
              {
                for (auto it = attrmap.begin(); it != attrmap.end(); it++)
                {
                  XrdOucString akey = it->first.c_str();
                  if (akey.matches(wkey.c_str()))
                  {
                    found[fpath].size();
                  }
                }
              }
              found_dirs[deepness + 1].push_back(fpath.c_str());
            }
            else
            {
              // this is a search for a full match or a key search

              std::string sval = val;
              XrdOucString attr = "";
              if (!gOFS->_attr_get(fpath.c_str(), out_error, vid,
                                   (const char*) 0, key, attr, true))
              {
                found_dirs[deepness + 1].push_back(fpath.c_str());
                if ((val == std::string("*")) || (attr == val))
                {
                  found[fpath].size();
                }
              }
            }
          }
          else
          {
            if (limitresult)
            {
              // apply the user limits for non root/admin/sudoers
              if (dirsfound >= finddiruserlimit)
              {
                stdErr += "warning: find results are limited for you to ndirs=";
                stdErr += (int) finddiruserlimit;
                stdErr += " -  result is truncated!\n";
                limited = true;
                break;
              }
            }
            found_dirs[deepness + 1].push_back(fpath.c_str());
            found[fpath].size();
            dirsfound++;
          }
        }

        if (!nofiles)
        {
          eos::ContainerMD::FileMap::iterator fit;
          for (fit = cmd->filesBegin(); fit != cmd->filesEnd(); ++fit)
          {
	    std::string link;
	    // skip symbolic links
	    if (fit->second->isLink())
	      link=fit->second->getLink();
            if (limitresult)
            {
              // apply the user limits for non root/admin/sudoers
              if (filesfound >= findfileuserlimit)
              {
                stdErr += "warning: find results are limited for you to nfiles=";
                stdErr += (int) findfileuserlimit;
                stdErr += " -  result is truncated!\n";
                limited = true;
                break;
              }
            }
	    if (!filematch)
	    {
	      if (link.length()) 
	      {
		std::string ip = fit->second->getName();
		ip += " -> ";
		ip += link;
		found[Path].insert(ip);
	      } 
	      else 
	      {
		found[Path].insert(fit->second->getName());
	      }
	      filesfound++;
	    }
	    else
	    {
	      XrdOucString name=fit->second->getName().c_str();
	      if (name.matches(filematch))
	      {
		found[Path].insert(fit->second->getName());
		filesfound++;
	      }
	    }
          }
        }
      }
      if (limited)
      {
        break;
      }
    }

    deepness++;
    if (limited)
    {
      break;
    }
  }
  while (found_dirs[deepness].size() && ( (!maxdepth) || (deepness < maxdepth)));
  // ---------------------------------------------------------------------------
  if (!nofiles)
  {
    // if the result is empty, maybe this was a find by file
    if (!found.size())
    {
      XrdSfsFileExistence file_exists;
      if (((_exists(Path.c_str(), file_exists, out_error, vid, 0)) == SFS_OK) &&
          (file_exists == XrdSfsFileExistIsFile))
      {
        eos::common::Path cPath(Path.c_str());
        found[cPath.GetParentPath()].insert(cPath.GetName());
      }
    }
  }
  // ---------------------------------------------------------------------------
  // include also the directory which was specified in the query if it is
  // accessible and a directory since it can evt. be missing if it is empty
  // ---------------------------------------------------------------------------
  XrdSfsFileExistence dir_exists;
  if (((_exists(found_dirs[0][0].c_str(), dir_exists, out_error, vid, 0)) == SFS_OK)
      && (dir_exists == XrdSfsFileExistIsDirectory))
  {

    eos::common::Path cPath(found_dirs[0][0].c_str());
    found[found_dirs[0][0].c_str()].size();
  }

  if (nscounter)
  {
    EXEC_TIMING_END("Find");
  }
  return SFS_OK;
}
