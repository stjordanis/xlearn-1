//------------------------------------------------------------------------------
// Copyright (c) 2016 by contributors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//------------------------------------------------------------------------------

/*
Author: Chao Ma (mctt90@gmail.com)

This file defines the Reader class that is responsible for
reading data from data source.
*/

#ifndef XLEARN_READER_READER_H_
#define XLEARN_READER_READER_H_

#include <string>
#include <vector>
#include <thread>

#include "src/base/common.h"
#include "src/base/class_register.h"
#include "src/base/scoped_ptr.h"
#include "src/base/thread_pool.h"
#include "src/data/data_structure.h"
#include "src/reader/parser.h"

namespace xLearn {

//------------------------------------------------------------------------------
// Reader is an abstract class which can be implemented in different way,
// such as the InmemReader that reads data from memory, and the OndiskReader
// that reads data from disk file for large-scale machine learning.
//
// We can use the Reader class like this (Pseudo code):
//
//   #include "reader.h"
//
//   /* or new InmemReader() */
//   Reader* reader = new OndiskReader();
//
//   /* For in-memory Reader, the buffer_size (MB) will not be used */
//   /* For on-disk Reader, the shuffle will always be false */
//  
//   reader->Initialize(filename = "/tmp/testdata",
//                      buffer_size = 200,
//                      shuffle = false);
//  
//   DMatrix* matrix = nullptr;
// 
//   Loop {
//
//      size_t num_samples = reader->Sample(DMatrix);
//
//      /* The reader will return 0 when reaching the end of
//      data source, and then we can invoke Reset() to return
//      to the begining of data */
//  
//      if (num_samples == 0) {
//        reader->Reset()
//      }
//
//      /* use data matrix ... */
//
//   }
//
// For now, the Reader can parse three kinds of file format, including
// the libsvm format, the libffm format, and the CSV format.
//------------------------------------------------------------------------------
class Reader {
 public:
  // Constructor and Desstructor
  Reader() : shuffle_(false) {  }
  virtual ~Reader() {  }

  // We need to invoke the Initialize() function before
  // we start to sample data. We can shuffle data before 
  // training, and this is good for SGD.
  virtual void Initialize(const std::string& filename) = 0;

  // Sample data from disk or from memory buffer.
  // Return the number of record in each samplling.
  // Samples() will return 0 when reaching end of the data.
  virtual index_t Samples(DMatrix* &matrix) = 0;

  // Return to the begining of the data.
  virtual void Reset() = 0;

  // Wether current dataset has label y ?
  bool inline has_label() { return has_label_; }

  // If shuffle data ?
  virtual void SetShuffle(bool shuffle) {
    this->shuffle_ = shuffle;
  }

 protected:
  /* Input file name */
  std::string filename_;
  /* Sample() returns this data sample */
  DMatrix data_samples_;
  /* Parser for a block of memory buffer */
  Parser* parser_;
  /* If this data has label y ?
  This value will be set automitically
  in initialization */
  bool has_label_;
  /* If shuffle data ? */
  bool shuffle_;

  // Check current file format and return
  // "libsvm", "ffm", or "csv".
  // Program crashes for unknow format.
  // This function will also check if current
  // data has the label y.
  std::string check_file_format();

  // Create parser for different file format
  Parser* CreateParser(const char* format_name) {
    return CREATE_PARSER(format_name);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(Reader);
};

//------------------------------------------------------------------------------
// Sampling data from memory buffer.
// For in-memory smaplling, the Reader will automatically convert
// txt data to binary data, and uses this binary data in the next time.
//------------------------------------------------------------------------------
class InmemReader : public Reader {
 public:
  // Constructor and Destructor
  InmemReader() : pos_(0) { }
  ~InmemReader() { data_buf_.Release(); }

  // Pre-load all the data into memory buffer.
  virtual void Initialize(const std::string& filename);

  // Sample data from the memory buffer.
  virtual index_t Samples(DMatrix* &matrix);

  // Return to the begining of the data.
  virtual void Reset();

  // If shuffle data ?
  virtual void SetShuffle(bool shuffle) {
    this->shuffle_ = shuffle;
    if (shuffle_ && !order_.empty()) {
      random_shuffle(order_.begin(), order_.end());
    }
  }

 protected:
  /* Reader will load all the data 
  into this buffer */
  DMatrix data_buf_;
  /* Number of record at each samplling */
  index_t num_samples_;
  /* Position for samplling */
  index_t pos_;
  /* For random shuffle */
  std::vector<index_t> order_;

  // Check wheter current path has a binary file.
  bool hash_binary(const std::string& filename);

  // Initialize Reader from existing binary file.
  void init_from_binary();

  // Initialize Reader from a new txt file.
  void init_from_txt();

 private:
  DISALLOW_COPY_AND_ASSIGN(InmemReader);
};

//------------------------------------------------------------------------------
// Samplling data from disk file.
// OndiskReader is used to train very big data, which cannot be
// loaded into main memory of current single machine. We use multi-thread 
// to support data pipeline to accelerate reading.
//------------------------------------------------------------------------------
class OndiskReader : public Reader {
 public:
  OndiskReader() :full_(false) {  }
  ~OndiskReader() { Close(file_ptr_); }

  // Allocate memory for block and pick up one thread 
  // from thread pool as back-thread, which will read 
  // and parse data asynchrously.
  virtual void Initialize(const std::string& filename);

  // Sample data from disk file
  virtual index_t Samples(DMatrix* &matrix);

  // Return to the begining of the file
  virtual void Reset();

  // We cannot set shuffle for OndiskReader
  void SetShuffle(bool shuffle) {
    if (shuffle == true) {
      LOG(ERROR) << "Cannot set shuffle for OndiskReader.";
    }
    this->shuffle_ = false;
  }

  /*********************************************************
   *  A set of get functions                               *
   *********************************************************/
  inline uint64 get_block_size() { return block_size_; }
  inline char* get_block() { return block_; }
  inline FILE* get_file_ptr() { return file_ptr_; }
  inline DMatrix* get_buffer() { return &data_buf_; }
  inline DMatrix* get_sample() { return &data_samples_; }
  inline Parser* get_parser() { return parser_; }

  /*********************************************************
   *  Note that we need to invoke these two functions      *
   *  before invoking the Initialize() function.           *
   *********************************************************/

  // Set thread pool
  void SetThreadPool(ThreadPool* pool) {
    CHECK_NOTNULL(pool);
    this->pool_ = pool;
  }
  // Set block size
  void SetBlockSize(uint64 size) {
    CHECK_GT(size, 0);
    this->block_size_ = size;
  }
 
 protected:
  /* Maintain the file pointer */
  FILE* file_ptr_;
  /* Reader will load a part of the 
  data into this buffer */
  DMatrix data_buf_;
  /* We pick up one thread from this pool
  to read and parse data */
  ThreadPool* pool_;
  /* A block of memory to store the data */
  char* block_;
  /* Block size */
  uint64 block_size_;
  /* Protect all data and condition */
  std::mutex mutex_;
  /* Condition when consumer should wait */
  std::condition_variable cond_not_full_;
  /* Condition when producer should wait */
  std::condition_variable cond_not_empty_;
  /* if has data in current DMatrix ? */
  bool full_;

  // Read a block of data from disk file and 
  // parse the data to DMatrix in a loop. 
  friend void read_block(OndiskReader* reader);
  friend void shrink_block(char* block, size_t ret, FILE* file_ptr);

 private:
  DISALLOW_COPY_AND_ASSIGN(OndiskReader);
};

//------------------------------------------------------------------------------
// Class register
//------------------------------------------------------------------------------
CLASS_REGISTER_DEFINE_REGISTRY(xLearn_reader_registry, Reader);

#define REGISTER_READER(format_name, reader_name)           \
  CLASS_REGISTER_OBJECT_CREATOR(                            \
      xLearn_reader_registry,                               \
      Reader,                                               \
      format_name,                                          \
      reader_name)

#define CREATE_READER(format_name)                          \
  CLASS_REGISTER_CREATE_OBJECT(                             \
      xLearn_reader_registry,                               \
      format_name)

} // namespace xLearn

#endif // XLEARN_READER_READER_H_
