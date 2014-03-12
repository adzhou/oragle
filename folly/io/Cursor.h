/*
 * Copyright 2014 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FOLLY_CURSOR_H
#define FOLLY_CURSOR_H

#include <assert.h>
#include <stdexcept>
#include <string.h>
#include <type_traits>
#include <memory>

#include "folly/Bits.h"
#include "folly/io/IOBuf.h"
#include "folly/io/IOBufQueue.h"
#include "folly/Likely.h"
#include "folly/Memory.h"

/**
 * Cursor class for fast iteration over IOBuf chains.
 *
 * Cursor - Read-only access
 *
 * RWPrivateCursor - Read-write access, assumes private access to IOBuf chain
 * RWUnshareCursor - Read-write access, calls unshare on write (COW)
 * Appender        - Write access, assumes private access to IOBuf chian
 *
 * Note that RW cursors write in the preallocated part of buffers (that is,
 * between the buffer's data() and tail()), while Appenders append to the end
 * of the buffer (between the buffer's tail() and bufferEnd()).  Appenders
 * automatically adjust the buffer pointers, so you may only use one
 * Appender with a buffer chain; for this reason, Appenders assume private
 * access to the buffer (you need to call unshare() yourself if necessary).
 **/
namespace folly { namespace io {
namespace detail {

template <class Derived, typename BufType>
class CursorBase {
 public:
  const uint8_t* data() const {
    return crtBuf_->data() + offset_;
  }

  /*
   * Return the remaining space available in the current IOBuf.
   *
   * May return 0 if the cursor is at the end of an IOBuf.  Use peek() instead
   * if you want to avoid this.  peek() will advance to the next non-empty
   * IOBuf (up to the end of the chain) if the cursor is currently pointing at
   * the end of a buffer.
   */
  size_t length() const {
    return crtBuf_->length() - offset_;
  }

  /*
   * Return the space available until the end of the entire IOBuf chain.
   */
  size_t totalLength() const {
    if (crtBuf_ == buffer_) {
      return crtBuf_->computeChainDataLength() - offset_;
    }
    CursorBase end(buffer_->prev());
    end.offset_ = end.buffer_->length();
    return end - *this;
  }

  Derived& operator+=(size_t offset) {
    Derived* p = static_cast<Derived*>(this);
    p->skip(offset);
    return *p;
  }

  template <class T>
  typename std::enable_if<std::is_arithmetic<T>::value, T>::type
  read() {
    T val;
    pull(&val, sizeof(T));
    return val;
  }

  template <class T>
  T readBE() {
    return Endian::big(read<T>());
  }

  template <class T>
  T readLE() {
    return Endian::little(read<T>());
  }

  /**
   * Read a fixed-length string.
   *
   * The std::string-based APIs should probably be avoided unless you
   * ultimately want the data to live in an std::string. You're better off
   * using the pull() APIs to copy into a raw buffer otherwise.
   */
  std::string readFixedString(size_t len) {
    std::string str;

    str.reserve(len);
    for (;;) {
      // Fast path: it all fits in one buffer.
      size_t available = length();
      if (LIKELY(available >= len)) {
        str.append(reinterpret_cast<const char*>(data()), len);
        offset_ += len;
        return str;
      }

      str.append(reinterpret_cast<const char*>(data()), available);
      if (UNLIKELY(!tryAdvanceBuffer())) {
        throw std::out_of_range("string underflow");
      }
      len -= available;
    }
  }

  /**
   * Read a string consisting of bytes until the given terminator character is
   * seen. Raises an std::length_error if maxLength bytes have been processed
   * before the terminator is seen.
   *
   * See comments in readFixedString() about when it's appropriate to use this
   * vs. using pull().
   */
  std::string readTerminatedString(
    char termChar = '\0',
    size_t maxLength = std::numeric_limits<size_t>::max()) {
    std::string str;

    for (;;) {
      const uint8_t* buf = data();
      size_t buflen = length();

      size_t i = 0;
      while (i < buflen && buf[i] != termChar) {
        ++i;

        // Do this check after incrementing 'i', as even though we start at the
        // 0 byte, it still represents a single character
        if (str.length() + i >= maxLength) {
          throw std::length_error("string overflow");
        }
      }

      str.append(reinterpret_cast<const char*>(buf), i);
      if (i < buflen) {
        skip(i + 1);
        return str;
      }

      skip(i);

      if (UNLIKELY(!tryAdvanceBuffer())) {
        throw std::out_of_range("string underflow");
      }
    }
  }

  explicit CursorBase(BufType* buf)
    : crtBuf_(buf)
    , offset_(0)
    , buffer_(buf) {}

  // Make all the templated classes friends for copy constructor.
  template <class D, typename B> friend class CursorBase;

  template <class T>
  explicit CursorBase(const T& cursor) {
    crtBuf_ = cursor.crtBuf_;
    offset_ = cursor.offset_;
    buffer_ = cursor.buffer_;
  }

  // reset cursor to point to a new buffer.
  void reset(BufType* buf) {
    crtBuf_ = buf;
    buffer_ = buf;
    offset_ = 0;
  }

  /**
   * Return the available data in the current buffer.
   * If you want to gather more data from the chain into a contiguous region
   * (for hopefully zero-copy access), use gather() before peek().
   */
  std::pair<const uint8_t*, size_t> peek() {
    // Ensure that we're pointing to valid data
    size_t available = length();
    while (UNLIKELY(available == 0 && tryAdvanceBuffer())) {
      available = length();
    }

    return std::make_pair(data(), available);
  }

  void pull(void* buf, size_t len) {
    if (UNLIKELY(pullAtMost(buf, len) != len)) {
      throw std::out_of_range("underflow");
    }
  }

  void clone(std::unique_ptr<folly::IOBuf>& buf, size_t len) {
    if (UNLIKELY(cloneAtMost(buf, len) != len)) {
      throw std::out_of_range("underflow");
    }
  }

  void clone(folly::IOBuf& buf, size_t len) {
    if (UNLIKELY(cloneAtMost(buf, len) != len)) {
      throw std::out_of_range("underflow");
    }
  }

  void skip(size_t len) {
    if (UNLIKELY(skipAtMost(len) != len)) {
      throw std::out_of_range("underflow");
    }
  }

  size_t pullAtMost(void* buf, size_t len) {
    uint8_t* p = reinterpret_cast<uint8_t*>(buf);
    size_t copied = 0;
    for (;;) {
      // Fast path: it all fits in one buffer.
      size_t available = length();
      if (LIKELY(available >= len)) {
        memcpy(p, data(), len);
        offset_ += len;
        return copied + len;
      }

      memcpy(p, data(), available);
      copied += available;
      if (UNLIKELY(!tryAdvanceBuffer())) {
        return copied;
      }
      p += available;
      len -= available;
    }
  }

  size_t cloneAtMost(folly::IOBuf& buf, size_t len) {
    buf = folly::IOBuf();

    std::unique_ptr<folly::IOBuf> tmp;
    size_t copied = 0;
    for (int loopCount = 0; true; ++loopCount) {
      // Fast path: it all fits in one buffer.
      size_t available = length();
      if (LIKELY(available >= len)) {
        if (loopCount == 0) {
          crtBuf_->cloneOneInto(buf);
          buf.trimStart(offset_);
          buf.trimEnd(buf.length() - len);
        } else {
          tmp = crtBuf_->cloneOne();
          tmp->trimStart(offset_);
          tmp->trimEnd(tmp->length() - len);
          buf.prependChain(std::move(tmp));
        }

        offset_ += len;
        return copied + len;
      }


      if (loopCount == 0) {
        crtBuf_->cloneOneInto(buf);
        buf.trimStart(offset_);
      } else {
        tmp = crtBuf_->cloneOne();
        tmp->trimStart(offset_);
        buf.prependChain(std::move(tmp));
      }

      copied += available;
      if (UNLIKELY(!tryAdvanceBuffer())) {
        return copied;
      }
      len -= available;
    }
  }

  size_t cloneAtMost(std::unique_ptr<folly::IOBuf>& buf, size_t len) {
    if (!buf) {
      buf = make_unique<folly::IOBuf>();
    }

    return cloneAtMost(*buf, len);
  }

  size_t skipAtMost(size_t len) {
    size_t skipped = 0;
    for (;;) {
      // Fast path: it all fits in one buffer.
      size_t available = length();
      if (LIKELY(available >= len)) {
        offset_ += len;
        return skipped + len;
      }

      skipped += available;
      if (UNLIKELY(!tryAdvanceBuffer())) {
        return skipped;
      }
      len -= available;
    }
  }

  /**
   * Return the distance between two cursors.
   */
  size_t operator-(const CursorBase& other) const {
    BufType *otherBuf = other.crtBuf_;
    size_t len = 0;

    if (otherBuf != crtBuf_) {
      len += otherBuf->length() - other.offset_;

      for (otherBuf = otherBuf->next();
           otherBuf != crtBuf_ && otherBuf != other.buffer_;
           otherBuf = otherBuf->next()) {
        len += otherBuf->length();
      }

      if (otherBuf == other.buffer_) {
        throw std::out_of_range("wrap-around");
      }

      len += offset_;
    } else {
      if (offset_ < other.offset_) {
        throw std::out_of_range("underflow");
      }

      len += offset_ - other.offset_;
    }

    return len;
  }

  /**
   * Return the distance from the given IOBuf to the this cursor.
   */
  size_t operator-(const BufType* buf) const {
    size_t len = 0;

    BufType *curBuf = buf;
    while (curBuf != crtBuf_) {
      len += curBuf->length();
      curBuf = curBuf->next();
      if (curBuf == buf || curBuf == buffer_) {
        throw std::out_of_range("wrap-around");
      }
    }

    len += offset_;
    return len;
  }

 protected:
  BufType* crtBuf_;
  size_t offset_;

  ~CursorBase(){}

  BufType* head() {
    return buffer_;
  }

  bool tryAdvanceBuffer() {
    BufType* nextBuf = crtBuf_->next();
    if (UNLIKELY(nextBuf == buffer_)) {
      offset_ = crtBuf_->length();
      return false;
    }

    offset_ = 0;
    crtBuf_ = nextBuf;
    static_cast<Derived*>(this)->advanceDone();
    return true;
  }

 private:
  void advanceDone() {
  }

  BufType* buffer_;
};

template <class Derived>
class Writable {
 public:
  template <class T>
  typename std::enable_if<std::is_arithmetic<T>::value>::type
  write(T value) {
    const uint8_t* u8 = reinterpret_cast<const uint8_t*>(&value);
    Derived* d = static_cast<Derived*>(this);
    d->push(u8, sizeof(T));
  }

  template <class T>
  void writeBE(T value) {
    Derived* d = static_cast<Derived*>(this);
    d->write(Endian::big(value));
  }

  template <class T>
  void writeLE(T value) {
    Derived* d = static_cast<Derived*>(this);
    d->write(Endian::little(value));
  }

  void push(const uint8_t* buf, size_t len) {
    Derived* d = static_cast<Derived*>(this);
    if (d->pushAtMost(buf, len) != len) {
      throw std::out_of_range("overflow");
    }
  }
};

} // namespace detail

class Cursor : public detail::CursorBase<Cursor, const IOBuf> {
 public:
  explicit Cursor(const IOBuf* buf)
    : detail::CursorBase<Cursor, const IOBuf>(buf) {}

  template <class CursorType>
  explicit Cursor(CursorType& cursor)
    : detail::CursorBase<Cursor, const IOBuf>(cursor) {}
};

enum class CursorAccess {
  PRIVATE,
  UNSHARE
};

template <CursorAccess access>
class RWCursor
  : public detail::CursorBase<RWCursor<access>, IOBuf>,
    public detail::Writable<RWCursor<access>> {
  friend class detail::CursorBase<RWCursor<access>, IOBuf>;
 public:
  explicit RWCursor(IOBuf* buf)
    : detail::CursorBase<RWCursor<access>, IOBuf>(buf),
      maybeShared_(true) {}

  template <class CursorType>
  explicit RWCursor(CursorType& cursor)
    : detail::CursorBase<RWCursor<access>, IOBuf>(cursor),
      maybeShared_(true) {}
  /**
   * Gather at least n bytes contiguously into the current buffer,
   * by coalescing subsequent buffers from the chain as necessary.
   */
  void gather(size_t n) {
    // Forbid attempts to gather beyond the end of this IOBuf chain.
    // Otherwise we could try to coalesce the head of the chain and end up
    // accidentally freeing it, invalidating the pointer owned by external
    // code.
    //
    // If crtBuf_ == head() then IOBuf::gather() will perform all necessary
    // checking.  We only have to perform an explicit check here when calling
    // gather() on a non-head element.
    if (this->crtBuf_ != this->head() && this->totalLength() < n) {
      throw std::overflow_error("cannot gather() past the end of the chain");
    }
    this->crtBuf_->gather(this->offset_ + n);
  }
  void gatherAtMost(size_t n) {
    size_t size = std::min(n, this->totalLength());
    return this->crtBuf_->gather(this->offset_ + size);
  }

  size_t pushAtMost(const uint8_t* buf, size_t len) {
    size_t copied = 0;
    for (;;) {
      // Fast path: the current buffer is big enough.
      size_t available = this->length();
      if (LIKELY(available >= len)) {
        if (access == CursorAccess::UNSHARE) {
          maybeUnshare();
        }
        memcpy(writableData(), buf, len);
        this->offset_ += len;
        return copied + len;
      }

      if (access == CursorAccess::UNSHARE) {
        maybeUnshare();
      }
      memcpy(writableData(), buf, available);
      copied += available;
      if (UNLIKELY(!this->tryAdvanceBuffer())) {
        return copied;
      }
      buf += available;
      len -= available;
    }
  }

  void insert(std::unique_ptr<folly::IOBuf> buf) {
    folly::IOBuf* nextBuf;
    if (this->offset_ == 0) {
      // Can just prepend
      nextBuf = this->crtBuf_;
      this->crtBuf_->prependChain(std::move(buf));
    } else {
      std::unique_ptr<folly::IOBuf> remaining;
      if (this->crtBuf_->length() - this->offset_ > 0) {
        // Need to split current IOBuf in two.
        remaining = this->crtBuf_->cloneOne();
        remaining->trimStart(this->offset_);
        nextBuf = remaining.get();
        buf->prependChain(std::move(remaining));
      } else {
        // Can just append
        nextBuf = this->crtBuf_->next();
      }
      this->crtBuf_->trimEnd(this->length());
      this->crtBuf_->appendChain(std::move(buf));
    }
    // Jump past the new links
    this->offset_ = 0;
    this->crtBuf_ = nextBuf;
  }

  uint8_t* writableData() {
    return this->crtBuf_->writableData() + this->offset_;
  }

 private:
  void maybeUnshare() {
    if (UNLIKELY(maybeShared_)) {
      this->crtBuf_->unshareOne();
      maybeShared_ = false;
    }
  }

  void advanceDone() {
    maybeShared_ = true;
  }

  bool maybeShared_;
};

typedef RWCursor<CursorAccess::PRIVATE> RWPrivateCursor;
typedef RWCursor<CursorAccess::UNSHARE> RWUnshareCursor;

/**
 * Append to the end of a buffer chain, growing the chain (by allocating new
 * buffers) in increments of at least growth bytes every time.  Won't grow
 * (and push() and ensure() will throw) if growth == 0.
 *
 * TODO(tudorb): add a flavor of Appender that reallocates one IOBuf instead
 * of chaining.
 */
class Appender : public detail::Writable<Appender> {
 public:
  Appender(IOBuf* buf, uint64_t growth)
    : buffer_(buf),
      crtBuf_(buf->prev()),
      growth_(growth) {
  }

  uint8_t* writableData() {
    return crtBuf_->writableTail();
  }

  size_t length() const {
    return crtBuf_->tailroom();
  }

  /**
   * Mark n bytes (must be <= length()) as appended, as per the
   * IOBuf::append() method.
   */
  void append(size_t n) {
    crtBuf_->append(n);
  }

  /**
   * Ensure at least n contiguous bytes available to write.
   * Postcondition: length() >= n.
   */
  void ensure(uint64_t n) {
    if (LIKELY(length() >= n)) {
      return;
    }

    // Waste the rest of the current buffer and allocate a new one.
    // Don't make it too small, either.
    if (growth_ == 0) {
      throw std::out_of_range("can't grow buffer chain");
    }

    n = std::max(n, growth_);
    buffer_->prependChain(IOBuf::create(n));
    crtBuf_ = buffer_->prev();
  }

  size_t pushAtMost(const uint8_t* buf, size_t len) {
    size_t copied = 0;
    for (;;) {
      // Fast path: it all fits in one buffer.
      size_t available = length();
      if (LIKELY(available >= len)) {
        memcpy(writableData(), buf, len);
        append(len);
        return copied + len;
      }

      memcpy(writableData(), buf, available);
      append(available);
      copied += available;
      if (UNLIKELY(!tryGrowChain())) {
        return copied;
      }
      buf += available;
      len -= available;
    }
  }

 private:
  bool tryGrowChain() {
    assert(crtBuf_->next() == buffer_);
    if (growth_ == 0) {
      return false;
    }

    buffer_->prependChain(IOBuf::create(growth_));
    crtBuf_ = buffer_->prev();
    return true;
  }

  IOBuf* buffer_;
  IOBuf* crtBuf_;
  uint64_t growth_;
};

class QueueAppender : public detail::Writable<QueueAppender> {
 public:
  /**
   * Create an Appender that writes to a IOBufQueue.  When we allocate
   * space in the queue, we grow no more than growth bytes at once
   * (unless you call ensure() with a bigger value yourself).
   */
  QueueAppender(IOBufQueue* queue, uint64_t growth) {
    reset(queue, growth);
  }

  void reset(IOBufQueue* queue, uint64_t growth) {
    queue_ = queue;
    growth_ = growth;
  }

  uint8_t* writableData() {
    return static_cast<uint8_t*>(queue_->writableTail());
  }

  size_t length() const { return queue_->tailroom(); }

  void append(size_t n) { queue_->postallocate(n); }

  // Ensure at least n contiguous; can go above growth_, throws if
  // not enough room.
  void ensure(uint64_t n) { queue_->preallocate(n, growth_); }

  template <class T>
  typename std::enable_if<std::is_arithmetic<T>::value>::type
  write(T value) {
    // We can't fail.
    auto p = queue_->preallocate(sizeof(T), growth_);
    storeUnaligned(p.first, value);
    queue_->postallocate(sizeof(T));
  }


  size_t pushAtMost(const uint8_t* buf, size_t len) {
    size_t remaining = len;
    while (remaining != 0) {
      auto p = queue_->preallocate(std::min(remaining, growth_),
                                   growth_,
                                   remaining);
      memcpy(p.first, buf, p.second);
      queue_->postallocate(p.second);
      buf += p.second;
      remaining -= p.second;
    }

    return len;
  }

  void insert(std::unique_ptr<folly::IOBuf> buf) {
    if (buf) {
      queue_->append(std::move(buf), true);
    }
  }

 private:
  folly::IOBufQueue* queue_;
  size_t growth_;
};

}}  // folly::io

#endif // FOLLY_CURSOR_H
