//------------------------------------------------------------------------------
// File: TableFormatterBase.cc
// Author: Ivan Arizanovic & Stefan Isidorovic - Comtrade
//------------------------------------------------------------------------------

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

#include "TableFormatterBase.hh"

//------------------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------------------
TableFormatterBase::TableFormatterBase()
  : mSink("")
{
}

//------------------------------------------------------------------------------
// Generate table
//------------------------------------------------------------------------------
std::string TableFormatterBase::GenerateTable(TableFormatterStyle style,
    const TableString& selections)
{
  Style(style);
  bool body_exist = false;

  // Generate monitoring information in line (option "-m")
  if (!mHeader.empty() &&
      std::get<2>(mHeader[0]).find("o") != std::string::npos) {
    body_exist = GenerateMonitoring();
  }

  // Generate classic table (with/without second table)
  if (!mHeader.empty() &&
      std::get<2>(mHeader[0]).find("o") == std::string::npos) {
    WidthCorrection();
    GenerateHeader();
    body_exist = GenerateBody(selections);
  }

  // Generate string (e.g.second table)
  if (mHeader.empty()) {
    body_exist = GenerateBody();
  }

  if (body_exist) {
    return mSink.str();
  } else {
    return "";
  }
}

//------------------------------------------------------------------------------
// Generate monitoring output
//------------------------------------------------------------------------------
bool TableFormatterBase::GenerateMonitoring()
{
  bool body_exist = false;

  for (auto& row : mData) {
    if (!row.empty()) {
      for (size_t i = 0, size = mHeader.size(); i < size; ++i) {
        mSink << std::get<0>(mHeader[i]) << "=" << row[i] << " ";
        body_exist = true;
      }

      mSink << "\n";
    }
  }

  return body_exist;
}

//------------------------------------------------------------------------------
// Generate table separator
//------------------------------------------------------------------------------
std::string
TableFormatterBase::GenerateSeparator(std::string left, std::string center,
                                      std::string right, std::string line)
{
  std::string separator = left;

  for (size_t i = 0, size = mHeader.size(); i < size; ++i) {
    for (size_t i2 = 0; i2 < std::get<1>(mHeader[i]); i2++) {
      separator += line;
    }

    if (i < size - 1) {
      separator += center;
    }
  }

  separator += right;
  return separator;
}

//------------------------------------------------------------------------------
// Generate table header
//------------------------------------------------------------------------------
void TableFormatterBase::GenerateHeader()
{
  // Top edge of header
  mSink << GenerateSeparator(mBorderHead[0], mBorderHead[1],
                             mBorderHead[2], mBorderHead[3])
        << std::endl;

  for (size_t i = 0, size = mHeader.size(); i < size; ++i) {
    // Left edge of header
    if (i == 0) {
      mSink << mBorderHead[4];
    }

    // Generate cell
    if (std::get<2>(mHeader[i]).find("-") == std::string::npos) {
      mSink.width(std::get<1>(mHeader[i]));
    }

    mSink << std::get<0>(mHeader[i]);

    if (std::get<2>(mHeader[i]).find("-") != std::string::npos) {
      mSink.width(std::get<1>(mHeader[i]) - std::get<0>(mHeader[i]).length() +
                  mBorderHead[5].length());
    }

    // Add right edge of cell
    if (i < size - 1) {
      mSink << mBorderHead[5];
    }
  }

  // Right edge of the header
  mSink << mBorderHead[6] << std::endl;
  // Bottom edge of the header
  mSink << GenerateSeparator(mBorderHead[7], mBorderHead[8], mBorderHead[9],
                             mBorderHead[10])
        << std::endl;
}

//------------------------------------------------------------------------------
// Generate table body
//------------------------------------------------------------------------------
bool TableFormatterBase::GenerateBody(const TableString& selections)
{
  size_t row_size = 0;
  size_t string_size = 0;
  bool body_exist = false;
  bool row_exist = true;  //true because alone string
  bool string_exist = false;

  for (auto& row : mData) {
    if (row.empty()) {
      // Generate string
      if (!mString[string_size].empty() && row_exist) {
        if (!mHeader.empty()) {
          if (row_size > 0 && !mData[row_size - 1].empty()) {
            // Bottom edge of table, before string
            mSink << GenerateSeparator(mBorderBody[3], mBorderBody[4],
                                       mBorderBody[5], mBorderBody[6])
                  << std::endl;
            mSink << mString[string_size];
            body_exist = true;
            string_exist = true;
          }
        }
        // If we have only string, without table
        else {
          mSink << mString[string_size];
          body_exist = true;
          string_exist = true;
        }
      }

      // Generate separator
      if (body_exist && !string_exist && selections.empty()) {
        mSink << GenerateSeparator(mBorderSep[0], mBorderSep[1],
                                   mBorderSep[2], mBorderSep[3])
              << std::endl;
      }

      string_size++;
    }

    // Generate rows
    if (!row.empty() && !mHeader.empty()) {
      std::stringstream mSink_temp;

      for (size_t i = 0, size = mHeader.size(); i < size; ++i) {
        // Left edge
        if (i == 0) {
          mSink_temp << mBorderBody[0];
        }

        // Change color of cell
        row[i].SetColor(ChangeColor(std::get<0>(mHeader[i]), row[i].Str()));
        // Generate cell
        size_t cellspace_width = std::get<1>(mHeader[i]) - row[i].Length();

        if (std::get<2>(mHeader[i]).find("-") == std::string::npos) {
          row[i].Print(mSink_temp, cellspace_width, 0);
        } else {
          row[i].Print(mSink_temp, 0, cellspace_width + mBorderBody[1].length());
        }

        // Right edge of cell
        if (i < size - 1) {
          mSink_temp << mBorderBody[1];
        }
      }

      // Right edge of row
      mSink_temp << mBorderBody[2] << std::endl;
      // Filter
      size_t filter_count = 0;

      for (size_t i = 0, size = selections.size(); i < size; ++i)
        if (mSink_temp.str().find(selections[i]) != std::string::npos) {
          filter_count++;
        }

      if (filter_count == selections.size()) {
        // Generate header if string exist before
        if (row_size > 0 && mData[row_size - 1].empty() &&
            string_size > 0 && !mString[string_size - 1].empty() && row_exist) {
          GenerateHeader();
        }

        // Generate row
        mSink << mSink_temp.str();
        body_exist = true;
        row_exist = true;
        string_exist = false;
      } else {
        row_exist = false;
      }
    }

    row_size++;
  }

  // Bottom edge
  if (!mHeader.empty() && !string_exist) {
    mSink << GenerateSeparator(mBorderBody[3], mBorderBody[4],
                               mBorderBody[5], mBorderBody[6])
          << std::endl;
  }

  return body_exist;
}

//------------------------------------------------------------------------------
// Recompute the width of the cells taking into account the data
//------------------------------------------------------------------------------
void TableFormatterBase::WidthCorrection()
{
  for (auto& row : mData) {
    if (!row.empty())
      for (size_t i = 0, size = mHeader.size(); i < size; i++) {
        if (std::get<1>(mHeader[i]) < std::get<0>(mHeader[i]).length()) {
          std::get<1>(mHeader[i]) = std::get<0>(mHeader[i]).length();
        }

        if (std::get<1>(mHeader[i]) < row[i].Length()) {
          std::get<1>(mHeader[i]) = row[i].Length();
        }
      }
  }
}

//------------------------------------------------------------------------------
// Set table header
//------------------------------------------------------------------------------
void TableFormatterBase::SetHeader(const TableHeader& header)
{
  if (mHeader.empty()) {
    mHeader = header;
  }
}

//------------------------------------------------------------------------------
// Add separator to table
//------------------------------------------------------------------------------
void TableFormatterBase::AddSeparator()
{
  mData.emplace_back();
  mString.emplace_back();
}

//------------------------------------------------------------------------------
// Add table data
//------------------------------------------------------------------------------
void TableFormatterBase::AddRows(const TableData& body)
{
  std::copy(body.begin(), body.end(), std::back_inserter(mData));
}

//------------------------------------------------------------------------------
// Add string
//------------------------------------------------------------------------------
void TableFormatterBase::AddString(std::string string)
{
  mData.emplace_back();
  mString.push_back(string);
}

//------------------------------------------------------------------------------
// Set cell color
//------------------------------------------------------------------------------
TableFormatterColor TableFormatterBase::ChangeColor(std::string header,
    std::string value)
{
  if (header == "status" || header == "active") {
    if (value == "online") {
      return BWHITE;
    }

    if (value == "offline" || value == "unknown") {
      return BBGRED;
    }
  }

  return DEFAULT;
}

//------------------------------------------------------------------------------
// Set table style
//------------------------------------------------------------------------------
void TableFormatterBase::Style(TableFormatterStyle style)
{
  switch (style) {
  // Full normal border [Default] ("│","┌","┬","┐","├","┼","┤","└","┴","┘","─")
  case FULL: {
    std::string head [11] = {"┌", "┬", "┐", "─",
                             "│", "│", "│",
                             "├", "┴", "┤", "─"
                            };
    std::string sep [4]   = {"│", "-", "│", "-"};
    std::string body [7]  = {"│", " ", "│",
                             "└", "─", "┘", "─"
                            };
    std::copy(head, head + 11, mBorderHead);
    std::copy(sep, sep + 4, mBorderSep);
    std::copy(body, body + 7, mBorderBody);
    break;
  }

  // Full bold border ("┃","┏","┳","┓","┣","╋","┫","┗","┻","┛","━")
  case FULLBOLD: {
    std::string head [11] = {"┏", "┳", "┓", "━",
                             "┃", "┃", "┃",
                             "┣", "┻", "┫", "━"
                            };
    std::string sep [4]   = {"┃", "-", "┃", "-"};
    std::string body [7]  = {"┃", " ", "┃",
                             "┗", "━", "┛", "━"
                            };
    std::copy(head, head + 11, mBorderHead);
    std::copy(sep, sep + 4, mBorderSep);
    std::copy(body, body + 7, mBorderBody);
    break;
  }

  // Full double border ("║","╔","╦","╗","╠","╬","╣","╚","╩","╝","═")
  case FULLDOUBLE: {
    std::string head [11] = {"╔", "╦", "╗", "═",
                             "║", "║", "║",
                             "╠", "╩", "╣", "═"
                            };
    std::string sep [4]   = {"║", "-", "║", "-"};
    std::string body [7]  = {"║", " ", "║",
                             "╚", "═", "╝", "═"
                            };
    std::copy(head, head + 11, mBorderHead);
    std::copy(sep, sep + 4, mBorderSep);
    std::copy(body, body + 7, mBorderBody);
    break;
  }

  // Header normal border ("│","┌","┬","┐","├","┼","┤","└","┴","┘","─")
  case HEADER: {
    std::string head [11] = {"┌", "┬", "┐", "─",
                             "│", "│", "│",
                             "└", "┴", "┘", "─"
                            };
    std::string sep [4]   = {" ", "-", " ", "-"};
    std::string body [7]  = {" ", " ", " "};
    std::copy(head, head + 11, mBorderHead);
    std::copy(sep, sep + 4, mBorderSep);
    std::copy(body, body + 7, mBorderBody);
    break;
  }

  // Header bold border ("┃","┏","┳","┓","┣","╋","┫","┗","┻","┛","━")
  case HEADERBOLD: {
    std::string head [11] = {"┏", "┳", "┓", "━",
                             "┃", "┃", "┃",
                             "┗", "┻", "┛", "━"
                            };
    std::string sep [4]   = {" ", "-", " ", "-"};
    std::string body [7]  = {" ", " ", " "};
    std::copy(head, head + 11, mBorderHead);
    std::copy(sep, sep + 4, mBorderSep);
    std::copy(body, body + 7, mBorderBody);
    break;
  }

  // Header double border ("║","╔","╦","╗","╠","╬","╣","╚","╩","╝","═")
  case HEADERDOUBLE: {
    std::string head [11] = {"╔", "╦", "╗", "═",
                             "║", "║", "║",
                             "╚", "╩", "╝", "═"
                            };
    std::string sep [4]   = {" ", "-", " ", "-"};
    std::string body [7]  = {" ", " ", " "};
    std::copy(head, head + 11, mBorderHead);
    std::copy(sep, sep + 4, mBorderSep);
    std::copy(body, body + 7, mBorderBody);
    break;
  }

  // Minimal style
  case MINIMAL: {
    std::string head [11] = {" ", "  ", " ", "-",
                             " ", "  ", " ",
                             " ", "  ", " ", "-"
                            };
    std::string sep [4]   = {" ", "  ", " ", "-"};
    std::string body [7]  = {" ", "  ", " "};
    std::copy(head, head + 11, mBorderHead);
    std::copy(sep, sep + 4, mBorderSep);
    std::copy(body, body + 7, mBorderBody);
    break;
  }

  // Old style
  case OLD: {
    std::string head [11] = {"#-", "--", "-", "-",
                             "# ", "# ", "#",
                             "#-", "--", "-", "-"
                            };
    std::string sep [4]   = {" -", "--", " ", "-"};
    std::string body [7]  = {"  ", "  ", " "};
    std::copy(head, head + 11, mBorderHead);
    std::copy(sep, sep + 4, mBorderSep);
    std::copy(body, body + 7, mBorderBody);
    break;
  }

  // Old style - wide
  case OLDWIDE: {
    std::string head [11] = {"#-", "---", "--", "-",
                             "# ", " # ", " #",
                             "#-", "---", "--", "-"
                            };
    std::string sep [4]   = {" -", "---", "- ", "-"};
    std::string body [7]  = {"  ", "   ", "  "};
    std::copy(head, head + 11, mBorderHead);
    std::copy(sep, sep + 4, mBorderSep);
    std::copy(body, body + 7, mBorderBody);
    break;
  }
  }
}
