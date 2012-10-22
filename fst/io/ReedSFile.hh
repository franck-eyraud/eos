// -----------------------------------------------------------------------------
// File: ReedSFile.hh
// Author: Elvin-Alin Sindrilaru - CERN
// -----------------------------------------------------------------------------

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

#ifndef __EOSFST_REEDSFILE_HH__
#define __EOSFST_REEDSFILE_HH__

/*----------------------------------------------------------------------------*/
#include "fst/Namespace.hh"
#include "fst/io/RaidIO.hh"
/*----------------------------------------------------------------------------*/

EOSFSTNAMESPACE_BEGIN

class ReedSFile : public RaidIO
{
  public:

    // -------------------------------------------------------------------------
    //! Constructor
    //!
    //! @param stripeurl URLs of the stripe files
    //! @param nparity number of parity stripes
    //! @param storerecovery if true write recovered blocks back to file
    //! @param targetsize expected final size (?!)
    //! @param bookingopaque opaque information
    //!
    // -------------------------------------------------------------------------
    ReedSFile( std::vector<std::string> stripeurl,
               int                      nparity,
               bool                     storerecovery,
               bool                     isstreaming,
               off_t                    targetsize = 0,
               std::string              bookingopaque = "oss.size" );

    // -------------------------------------------------------------------------
    //! Truncate file
    //!
    //! @param offset truncate size value
    //!
    //! @return 0 if successful, otherwise error
    //!
    // -------------------------------------------------------------------------
    virtual int truncate( off_t offset );

    // -------------------------------------------------------------------------
    //! Destructor
    // -------------------------------------------------------------------------
    virtual ~ReedSFile();

  private:

    // -------------------------------------------------------------------------
    //! Compute error correction blocks
    // -------------------------------------------------------------------------
    virtual void ComputeParity();

    // -------------------------------------------------------------------------
    //! Write parity information corresponding to a group to files
    //!
    //! @param offsetGroup offset of the group of blocks
    //!
    //! @return 0 if successful, otherwise error
    //!
    // -------------------------------------------------------------------------
    virtual int WriteParityToFiles( off_t offsetGroup );

    // -------------------------------------------------------------------------
    //! Recover pieces of corrupted data
    //!
    //! @param offsetInit file offset corresponding to byte 0 from the buffer
    //! @param buffer place where to save the recovered piece
    //! @mapPiece map of pieces to be recovered <offset in file, length>
    //!
    //! @return true if recovery was successful, otherwise false
    //!
    // -------------------------------------------------------------------------
    virtual bool RecoverPieces( off_t                    offset,
                                char*                    buffer,
                                std::map<off_t, size_t>& mapPieces );

    // -------------------------------------------------------------------------
    //! Add data block to compute parity stripes for current group of blocks
    //!
    //! @param offset block offset
    //! @param buffer data buffer
    //! @param length data length
    //!
    // -------------------------------------------------------------------------
    virtual void AddDataBlock( off_t offset, char* buffer, size_t length );

    // -------------------------------------------------------------------------
    //! Map index from nDataBlocks representation to nTotalBlocks
    //!
    //! @param idSmall with values between 0 and nDataBlocks
    //!
    //! @return index with the same values as idSmall, identical function
    //!
    // -------------------------------------------------------------------------
    virtual unsigned int MapSmallToBig( unsigned int idSmall );

    // -------------------------------------------------------------------------
    //! Get backtracking solution
    // -------------------------------------------------------------------------
    bool SolutionBkt( unsigned int         k,
                      unsigned int*        indexes,
                      vector<unsigned int> validId );

    // -------------------------------------------------------------------------
    //! Validate backtracking solution
    // -------------------------------------------------------------------------
    bool ValidBkt( unsigned int         k,
                   unsigned int*        indexes,
                   vector<unsigned int> validId );

    // -------------------------------------------------------------------------
    //! Backtracking method for getting the indices used in the recovery process
    // -------------------------------------------------------------------------
    bool Backtracking( unsigned int         k,
                       unsigned int*        indexes,
                       vector<unsigned int> validId );

};

EOSFSTNAMESPACE_END

#endif
