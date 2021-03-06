/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010- Facebook, Inc. (http://www.facebook.com)         |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/

#include "hphp/php7/ast_dump.h"
#include "hphp/php7/compiler.h"
#include "hphp/php7/zend/zend_language_scanner_defs.h"
#include "hphp/php7/bytecode.h"
#include "hphp/php7/hhas.h"

#include <folly/Format.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>

#include <string>
#include <iostream>

static constexpr size_t read_chunk = 1024;
static constexpr size_t allocate_chunk = 4096;

std::unique_ptr<folly::IOBuf> readAll(std::istream& in) {
  folly::IOBufQueue queue;

  // load data from the file into memory
  while (!in.eof()) {
    size_t realsize;
    void* buf;
    std::tie(buf, realsize) = queue.preallocate(read_chunk, allocate_chunk);
    in.read(static_cast<char*>(buf), realsize);
    size_t written = in.gcount();
    queue.postallocate(written);
  }

  // add the tailroom the lexer expects
  void* tail = queue.allocate(ZEND_MMAP_AHEAD);
  memset(tail, 0, ZEND_MMAP_AHEAD);

  // coalesce and return the buffer
  auto buf = queue.move();
  buf->coalesce();
  buf->trimEnd(ZEND_MMAP_AHEAD);
  return buf;
}

zend_ast* runParser(const folly::IOBuf& buffer) {
  init_parser_state();
  startup_scanner();

  unsigned char* buf =
    const_cast<unsigned char*>(buffer.data());
  LANG_SCNG(yy_cursor) = buf;
  LANG_SCNG(yy_limit) = buf + buffer.length();
  LANG_SCNG(yy_start) = buf;
  LANG_SCNG(yy_state) = yycINITIAL;
  CG(ast_arena) = zend_arena_create(256);

  zendparse();
  // dump AST to stderr
  HPHP::php7::dump_ast(std::cerr, CG(ast));
  std::cerr << std::endl;
  return CG(ast);
}

int main(int argc, const char** argv) {
  auto buf = readAll(std::cin);
  auto ast = runParser(*buf);
  auto unit = HPHP::php7::Compiler::compile(ast);
  auto hhas = HPHP::php7::dump_asm(*unit);

  std::cout << hhas << std::endl;

  return 0;
}
