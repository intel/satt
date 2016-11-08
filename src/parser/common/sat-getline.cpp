/*
// Copyright (c) 2015 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#include "sat-getline.h"

namespace sat {

bool getline(istream& stream, istringstream& line)
{
    string l;
    bool   got_it;

   do {
      got_it = static_cast<bool>(getline(stream, l));
   } while (got_it && l.size() > 0 && l[0] == '#');

   if (got_it) {
       line.seekg(0);
       line.str(l);
   }

    return got_it;
}

bool get_tagged_line(istream& stream, string& tag, istringstream& line)
{
    string l;
    bool   got_it = false;

    while (true) {
        if (!getline(stream, l)) {
            break;
        }

        line.clear();
        line.str(l);
        if (line >> skipws >> tag && tag[0] != '#') {
            got_it = true;
            break;
        }
    }

    return got_it;
}

string quote(const string& s)
{
    ostringstream os;
    os << '"';
    for (unsigned char c : s) {
        switch (c) {
         case '"':
             os << "\\\"";
             break;
         case '\n':
             os << "\\n";
             break;
         case '\\':
             os << "\\\\";
             break;
         default:
             os << c;
        }
    }
    os << '"';
    return os.str();
}

bool dequote(istringstream& is, string& s)
{
    bool done = false;
    s         = "";

    unsigned char c;
    if (is >> skipws >> c && c == '"' && is >> noskipws >> c) {
        do {
            if (c == '"') {
                done = true;
                break;
            } else if (c == '\\') {
                if (is >> c) {
                    if (c == '"' || c == '\\') {
                        s += c;
                    } else if (c == 'n') {
                        s += '\n';
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            } else {
                s += c;
            }
        } while (is >> c);
    }

    return done;
}

} // sat
