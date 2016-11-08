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
#ifndef SAT_FILE_INPUT_H
#define SAT_FILE_INPUT_H

#include <string>
#ifndef NO_SILLY_STATISTICS
#include <chrono>
#endif

namespace sat {

    using namespace std;

    class readable_file
    {
        public:
            readable_file() : file_() {}

            ~readable_file()
            {
                if (file_) {
                    (void)fclose(file_);
                }
            }

            bool open(const string& path)
            {
                if (path == "-") {
                    file_ = stdin;
                } else if ((file_ = fopen(path.c_str(), "r")) == 0) {
                    fprintf(stderr, "cannot open '%s' for reading\n", path.c_str());
                }

                return file_;
            }

        protected:
            FILE* file_;
    };


    class line_based_file : public readable_file
    {
        public:
            bool get_line(char** line, size_t* n)
            {
                return ::getline(line, n, file_) != -1;
            }
    };


    template <typename PARENT>
        class file_input : public readable_file, public PARENT
    {
        public:
            bool get_next(unsigned char& byte)
            {
                int c;
                if (file_ && (c = fgetc(file_)) != EOF) {
                    byte = c;
#ifndef NO_SILLY_STATISTICS
                    static unsigned count = 0;
                    if (!(count++ % 100000)) {
                        using namespace chrono;
                        static system_clock::time_point t{};

                        if (count == 1) {
                            t = system_clock::now();
                        } else {
                            auto new_t = system_clock::now();
                            auto m = duration_cast<milliseconds>(new_t - t);
                            fprintf(stderr,
                                    "%8u bytes in %li ms\n",
                                    count-1, m.count());
                            fflush(stderr);
                            t = new_t;
                        }
                    }
#endif
                    return true;
                } else {
                    return false;
                }
            }

            bool read(unsigned size, void* buffer)
            {
                if (!file_ || fread(buffer, 1, size, file_) < size) {
                    return false; // error: did not get enough data
                    // TODO: differentiate between zero and partial reads
                } else {
                    return true;
                }
            }

            bool eof() { return !file_ || feof(file_); }

            bool bad() { return !file_ || ferror(file_); }
    };

}

#endif
