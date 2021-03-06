// ----------------------------------------------------------------------
//! @file: XrdErrMap.hh
//! @author: Elvin-Alin Sindrilaru <esindril@cern.ch>
// ----------------------------------------------------------------------

/************************************************************************
 * EOS - the CERN Disk Storage System                                   *
 * Copyright (C) 2017 CERN/Switzerland                                  *
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

//------------------------------------------------------------------------
//! @brief  XRootD error to errno translation
//------------------------------------------------------------------------
#pragma once
#include "XProtocol/XProtocol.hh"
#include "common/Namespace.hh"

EOSCOMMONNAMESPACE_BEGIN

int error_retc_map(int retc);

EOSCOMMONNAMESPACE_END
