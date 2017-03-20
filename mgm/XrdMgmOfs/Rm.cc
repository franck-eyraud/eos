// ----------------------------------------------------------------------
// File: Rem.cc
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
XrdMgmOfs::rem(const char* inpath,
               XrdOucErrInfo& error,
               const XrdSecEntity* client,
               const char* ininfo)
/*----------------------------------------------------------------------------*/
/*
 * @brief delete a file from the namespace
 *
 * @param inpath file to delete
 * @param error error object
 * @param client XRootD authenticiation object
 * @param ininfo CGI
 * @return SFS_OK if success otherwise SFS_ERROR
 *
 * Deletion supports a recycle bin. See internal implementation of _rem for details.
 */
/*----------------------------------------------------------------------------*/

{
  static const char* epname = "rem";
  const char* tident = error.getErrUser();
  // use a thread private vid
  eos::common::Mapping::VirtualIdentity vid;
  EXEC_TIMING_BEGIN("IdMap");
  eos::common::Mapping::IdMap(client, ininfo, tident, vid);
  EXEC_TIMING_END("IdMap");
  NAMESPACEMAP;
  BOUNCE_ILLEGAL_NAMES;
  XrdOucEnv env(ininfo);
  AUTHORIZE(client, &env, AOP_Delete, "remove", inpath, error);
  gOFS->MgmStats.Add("IdMap", vid.uid, vid.gid, 1);
  BOUNCE_NOT_ALLOWED;
  ACCESSMODE_W;
  MAYSTALL;
  MAYREDIRECT;
  return _rem(path, error, vid, ininfo);
}

/*----------------------------------------------------------------------------*/
int
XrdMgmOfs::_rem(const char* path,
                XrdOucErrInfo& error,
                eos::common::Mapping::VirtualIdentity& vid,
                const char* ininfo,
                bool simulate,
                bool keepversion,
                bool no_recycling)
/*----------------------------------------------------------------------------*/
/*
 * @brief delete a file from the namespace
 *
 * @param inpath file to delete
 * @param error error object
 * @param vid virtual identity of the client
 * @param ininfo CGI
 * @param simulate indicates 'simulate deletion' e.g. it can be used as a test if a deletion would succeed
 * @param keepversion indicates if the deletion should wipe the version directory
 * @return SFS_OK if success otherwise SFS_ERROR
 *
 * Deletion supports the recycle bin if configured on the parent directory of
 * the file to be deleted. The simulation mode is used to test if there is
 * enough space in the recycle bin to move the object. If the simulation succeeds
 * the real deletion is executed. 'keepversion' is needed when we want to recover
 * an old version into the current version
 */
/*----------------------------------------------------------------------------*/
{
  static const char* epname = "rem";
  EXEC_TIMING_BEGIN("Rm");
  eos_info("path=%s vid.uid=%u vid.gid=%u", path, vid.uid, vid.gid);

  if (!simulate) {
    gOFS->MgmStats.Add("Rm", vid.uid, vid.gid, 1);
  }

  // Perform the actual deletion
  //
  errno = 0;
  XrdSfsFileExistence file_exists;

  if ((_exists(path, file_exists, error, vid, 0))) {
    return SFS_ERROR;
  }

  if (file_exists != XrdSfsFileExistIsFile) {
    if (file_exists == XrdSfsFileExistIsDirectory) {
      errno = EISDIR;
    } else {
      errno = ENOENT;
    }

    return Emsg(epname, error, errno, "remove", path);
  }

  // ---------------------------------------------------------------------------
  gOFS->eosViewRWMutex.LockWrite();
  // free the booked quota
  std::shared_ptr<eos::IFileMD> fmd;
  std::shared_ptr<eos::IContainerMD> container;
  eos::IContainerMD::XAttrMap attrmap;
  uid_t owner_uid = 0;
  gid_t owner_gid = 0;
  eos::common::FileId::fileid_t fid = 0;
  bool doRecycle = false; // indicating two-step deletion via recycle-bin
  std::string aclpath;

  try {
    fmd = gOFS->eosView->getFile(path, false);
  } catch (eos::MDException& e) {
    errno = e.getErrno();
    eos_debug("msg=\"exception\" ec=%d emsg=\"%s\"\n", e.getErrno(),
              e.getMessage().str().c_str());
  }

  if (fmd) {
    owner_uid = fmd->getCUid();
    owner_gid = fmd->getCGid();
    fid = fmd->getId();
    eos_info("got fmd=%lld", (unsigned long long) fmd.get());

    try {
      container = gOFS->eosDirectoryService->getContainerMD(fmd->getContainerId());
      aclpath = gOFS->eosView->getUri(container.get());
      eos_info("got container=%lld", (unsigned long long) container.get());
    } catch (eos::MDException& e) {
      container.reset();
    }

    // ACL and permission check
    Acl acl(aclpath.c_str(), error, vid, attrmap, false);
    eos_info("acl=%s mutable=%d", attrmap["sys.acl"].c_str(), acl.IsMutable());

    if (vid.uid && !acl.IsMutable()) {
      errno = EPERM;
      gOFS->eosViewRWMutex.UnLockWrite();
      return Emsg(epname, error, errno, "remove file - immutable", path);
    }

    bool stdpermcheck = false;

    if (acl.HasAcl()) {
      eos_info("acl=%d r=%d w=%d wo=%d egroup=%d delete=%d not-delete=%d mutable=%d",
               acl.HasAcl(), acl.CanRead(), acl.CanWrite(), acl.CanWriteOnce(),
               acl.HasEgroup(), acl.CanDelete(), acl.CanNotDelete(), acl.IsMutable());

      if ((!acl.CanWrite()) && (!acl.CanWriteOnce())) {
        // we have to check the standard permissions
        stdpermcheck = true;
      }
    } else {
      stdpermcheck = true;
    }

    if (container) {
      if (stdpermcheck && (!container->access(vid.uid, vid.gid, W_OK | X_OK))) {
        errno = EPERM;
        gOFS->eosViewRWMutex.UnLockWrite();
        std::ostringstream oss;
        oss << path << " by tident=" << vid.tident;
        return Emsg(epname, error, errno, "remove file", oss.str().c_str());
      }

      // check if this directory is write-once for the mapped user
      if (acl.CanWriteOnce() && (fmd->getSize())) {
        gOFS->eosViewRWMutex.UnLockWrite();
        errno = EPERM;
        // this is a write once user
        return Emsg(epname, error, EPERM,
                    "remove existing file - you are write-once user");
      }

      // if there is a !d policy we cannot delete files which we don't own
      if (((vid.uid) && (vid.uid != 3) && (vid.gid != 4) && (acl.CanNotDelete())) &&
          ((fmd->getCUid() != vid.uid))) {
        gOFS->eosViewRWMutex.UnLockWrite();
        errno = EPERM;
        // deletion is forbidden for not-owner
        return Emsg(epname, error, EPERM,
                    "remove existing file - ACL forbids file deletion");
      }

      if ((!stdpermcheck) && (!acl.CanWrite())) {
        gOFS->eosViewRWMutex.UnLockWrite();
        errno = EPERM;
        // this user is not allowed to write
        return Emsg(epname, error, EPERM,
                    "remove existing file - you don't have write permissions");
      }

      // -----------------------------------------------------------------------
      // check if there is a recycling bin specified and avoid recycling of the
      // already recycled files/dirs
      // -----------------------------------------------------------------------
      XrdOucString sPath = path;

      if (!(no_recycling) && attrmap.count(Recycle::gRecyclingAttribute) &&
          (!sPath.beginswith(Recycle::gRecyclingPrefix.c_str()))) {
        // ---------------------------------------------------------------------
        // this is two-step deletion via a recyle bin
        // ---------------------------------------------------------------------
        doRecycle = true;
      } else {
        // ---------------------------------------------------------------------
        // this is one-step deletion just removing files 'forever' and now
        // ---------------------------------------------------------------------
        if (!simulate) {
          try {
            eos::IQuotaNode* ns_quota = gOFS->eosView->getQuotaNode(container.get());
            eos_info("got quota node=%lld", (unsigned long long) ns_quota);

            if (ns_quota) {
              ns_quota->removeFile(fmd.get());
            }
          } catch (eos::MDException& e) { }
        }
      }
    }
  }

  if (!doRecycle) {
    try {
      if (!simulate) {
        eos_info("unlinking from view %s", path);
        gOFS->eosView->unlinkFile(path);
        // Reload file object that was modifed in the unlinkFile method
        // TODO: this can be dropped if you use the unlinkFile which takes
        // as argument the IFileMD object
        fmd = gOFS->eosFileService->getFileMD(fmd->getId());

        if ((!fmd->getNumUnlinkedLocation()) && (!fmd->getNumLocation())) {
          gOFS->eosView->removeFile(fmd.get());
        }

        if (container) {
          container->setMTimeNow();
          container->notifyMTimeChange(gOFS->eosDirectoryService);
          eosView->updateContainerStore(container.get());
        }
      }

      errno = 0;
    } catch (eos::MDException& e) {
      errno = e.getErrno();
      eos_debug("msg=\"exception\" ec=%d emsg=\"%s\"",
                e.getErrno(), e.getMessage().str().c_str());
    }
  }

  if (doRecycle && (!simulate)) {
    // Two-step deletion recycle logic
    XrdOucString recyclePath;
    gOFS->eosViewRWMutex.UnLockWrite();
    // -------------------------------------------------------------------------
    std::string recycle_space = attrmap[Recycle::gRecyclingAttribute].c_str();

    if (Quota::ExistsResponsible(recycle_space)) {
      if (!Quota::Check(recycle_space, fmd->getCUid(), fmd->getCGid(),
                        fmd->getSize(), fmd->getNumLocation())) {
        // This is the very critical case where we have to reject the delete
        // since the recycle space is full
        errno = ENOSPC;
        return Emsg(epname, error, ENOSPC, "remove existing file - the recycle "
                    "space is full");
      } else {
        // Move the file to the recycle bin
        eos::common::Mapping::VirtualIdentity rootvid;
        eos::common::Mapping::Root(rootvid);
        int rc = 0;
        Recycle lRecycle(path, attrmap[Recycle::gRecyclingAttribute].c_str(),
                         &vid, fmd->getCUid(), fmd->getCGid(),
                         fmd->getId());

        if ((rc = lRecycle.ToGarbage(epname, error))) {
          return rc;
        } else {
          recyclePath = error.getErrText();
        }
      }
    } else {
      // There is no quota defined on that recycle path
      errno = ENODEV;
      return Emsg(epname, error, ENODEV, "remove existing file - the recycle "
                  "space has no quota configuration");
    }

    if (!keepversion) {
      // call the version purge function in case there is a version (without gQuota locked)
      eos::common::Path cPath(path);
      XrdOucString vdir;
      vdir += cPath.GetVersionDirectory();

      // tag the version directory key on the garbage file
      if (recyclePath.length()) {
        eos::common::Mapping::VirtualIdentity rootvid;
        eos::common::Mapping::Root(rootvid);
        struct stat buf;

        if (!gOFS->_stat(vdir.c_str(), &buf, error, rootvid, 0, 0)) {
          char sp[256];
          snprintf(sp, sizeof(sp) - 1, "%016llx", (unsigned long long) buf.st_ino);

          if (gOFS->_attr_set(recyclePath.c_str(), error, vid, "",
                              Recycle::gRecyclingVersionKey.c_str(), sp)) {
            eos_err("msg=\"failed to set attribute on recycle path\" path=%s",
                    recyclePath.c_str());
          }
        } else {
          eos_err("msg\"failed to stat recycle path\" path=%s", recyclePath.c_str());
        }
      }

      gOFS->PurgeVersion(vdir.c_str(), error, 0);
      error.clear();
      errno = 0; // purge might return ENOENT if there was no version
    }
  } else {
    gOFS->eosViewRWMutex.UnLockWrite();

    if ((!errno) && (!keepversion)) {
      // call the version purge function in case there is a version (without gQuota locked)
      eos::common::Path cPath(path);
      XrdOucString vdir;
      vdir += cPath.GetVersionDirectory();
      gOFS->PurgeVersion(vdir.c_str(), error, 0);
      error.clear();
      errno = 0; // purge might return ENOENT if there was no version
    }
  }

  EXEC_TIMING_END("Rm");

  if (errno) {
    return Emsg(epname, error, errno, "remove", path);
  } else {
    Workflow workflow;
    // eventually trigger a workflow
    workflow.Init(&attrmap);
    workflow.SetFile(path, fid);
    int ret_wfe = 0;

    if ((ret_wfe = workflow.Trigger("delete", "default", vid)) < 0) {
      eos_debug("msg=\"no workflow defined for delete\"");
    } else {
      eos_debug("msg=\"workflow trigger returned\" retc=%d", ret_wfe);
    }

    eos_info("msg=\"deleted\" can-recycle=%d path=%s owner.uid=%u owner.gid=%u vid.uid=%u vid.gid=%u",
             doRecycle, path, owner_uid, owner_gid, vid.uid, vid.gid);
    return SFS_OK;
  }
}
