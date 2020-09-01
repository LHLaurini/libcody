# libCODY: COmpiler DYnamism<sup><a href="#1">1</a></sup>

Copyright (C) 2020 Nathan Sidwell, nathan@acm.org

libCODY is an implementation of a comminication protocol between
compilers and build systems.

**WARNING:**  This is preliminary software.

In addition to supporting C++modules, this may also support LTO
requirements and could also feed deal with generated #include files
and feed the compiler with prepruned include paths and whatnot.  (The
system calls involved in include searches can be quite expensive on
some build infrastuctures.)

* Client and Server objects
* Direct connection for in-process use
* Testing with Joust (that means nothing to you, doesn't it!)


## Problem Being Solved

The origin is in C++20 modules:
```
import foo;
```

At that import, the compiler needs<sup><a href="#2">2</a></sup> to
load up the compiled serialization of module `foo`.  Where is that
file?  Does it even exist?  Unless the build system already knows the
dependency graph, this might be a completely unknown module.  Now, the
build system knows how to build things, but it might not have complete
information about the dependencies.  The ultimate source of
dependencies is the source code being compiled, and specifying the
same thing in multiple places is a recipe for build skew.

Hence, a protocol by which a compiler can query a build system.  This
was originally described in <a
href="https://wg21.link/p1184r1">p1184r1:A Module Mapper</a>.  Along
with a proof-of-concept hack in GNUmake, described in <a
href="https://wg21.link/p1602">p1602:Make Me A Module</a>. The current
implementation has evolved and an update to p1184 will be forthcoming.

## Packet Encoding

The protocol is turn-based.  The compiler sends a block of one or more
requests to the builder, then waits for a block of responses to all of
those requests.  If the builder needs to compile something to satisfy
a request, there may be some time before the response.  A builder may
service multiple compilers concurrently, and it'll need some buffering
scheme to deal with that.

When multiple requests are in a block, the responses are also in a
block, and in corresponding order.

Every request has a response.

Requests and responses are user-readable text.  It is not intended as
a transmission medium to send large binary objects (such as compiled
modules).  It is presumed the builder and the compiler share a file
system, for that kind of thing.<sup><a href="#3">3</a></sup>

Messages characters are encoded in UTF8.

Messages are a sequenc of octets ending with a NEWLINE (0xa).  The lines
consist of a sequence of words, separated by WHITESPACE (0x20 or 0x9).
Words themselves do not contain WHITESPACE.  Lines consisting solely
of WHITESPACE (or empty) are ignored.

To encode a block of multiple messages, non-final messages end with a
single word of SEMICOLON (0x3b), immediately before the NEWLINE.  Thus
a serial connection can determine whether a block is complete without
decoding the messages.

Words containing characters in the set [-+_/%.A-Za-z0-9] need not be
quoted.  Words containing characters outside that set should be
quoted.  A zero-length word may be achieved with `''`

Quoted words begin and end with APOSTROPHE (x27). Within the quoted
word, BACKSLASH (x5c) is used as an escape mechanism, with the
following meanings:

* \\n - NEWLINE (0xa)
* \\t - TAB (0x9)
* \\' - APOSTROPHE (')
* \\\\ - BACKSLASH (\\)

Characters in the range [0x00, 0x20) and 0x7f are encoded with one or
two lowercase hex characters.  Octets in the range [0x80,0xff) are
UTF8 encodings of unicode characters outside the traditional ASCII set
and passed as such.

Decoding should be more relaxed.  Unquoted words containing characters
in the range [0x20,0xff] other than BACKSLASH or APOSTROPHE should be
accepted.  In a quoted sequence, `\` followed by one or two lower case
hex characters decode to that octet.  Further, words can be
constructed from a mixture of abutted quoted and unquoted sequences.
For instance `FOO' 'bar` would decode to the word `FOO bar`.

Notice that the block continuation marker of `;` is not a valid
encoding of the word `;`, which would be `';'`.

It is recommended that words are separated by single SPACE characters.

## Messages

The message descriptions use `$metavariable` examples.

All messages may result in an error response:

`ERROR $message`

The message is a human-readable string.

### Handshake

The first message is a handshake:

`HELLO $version $compiler $ident`

The `$version` is a numeric value, currently `1`.  `$compiler` identifies
the compiler &mdash; builders may need to keep compiled modules from
different compilers separate.  `$ident` is an identifer the builder
might use to identify the compilation it is communicting with.

Responses are:

`HELLO $version $builder`

A successful handshake.  The communication is now connected and other
messages may be exchanged.

`ERROR $message`

An unsuccesful handshake.  The communication remains unconnected.

There is nothing restricting a handshake to its own message block.  Of
course, if the handshake fails, subsequent non-handshake messages in
the block will fail (producing error responses).

### C++ Module Messages

A set of messages are specific to C++ modules

#### Repository

All relative CMI file names are relative to a repository.  (There are
usually no abosolute CMI files).  The respository may be determined
with:

`MODULE-REPO`

The expected response is:

`MODULE-REPO $directory`

The `$directory` may be an empty word, which is equivalent to `.`.

#### Exporting

A compilation of a module interface, partition or header unit can
inform the builder with:

`MODULE-EXPORT $module`

This will result in a response naming the Compiled Module Interface
file to write:

`MODULE-CMI $cmi`

The `MODULE-EXPORT` request does not indicate the module has been
successfully compiled.  At most one `MODULE-EXPORT` is to be made, and
as the connection is for a single compilation, the builder may infer
dependency relationships between the module being generated and import
requests made.

Named module names and header unit names are distinguished by making
the latter unambiguously look like file names.  Firstly, they must be
fully resolved according to the compiler's usual include path.  If
that results in an absolute name file name (beginning with `/`, or
certain other OS-specific sequences), all is well.  Otherwise a
relative file name must be prefixed by `./` to be distinguished from a
similarly named named module.  This prefixing must occur, even if the
header-unit's name contains characters that cannot appear in a named
module's name.

It is expected that absolute header-unit names convert to relative CMI
names, to keep all CMIs within the CMI repository.  This means that
steps must be taken to distinguish the CMIs for `/here` from `./here`,
and this can be achieved by replacing the leading `./` directory with
`,/`, which is visually similar but does not have the self-reference
semantics of dot.  Likewise, header-unit names containing `..`
directories, can be remapped to `,,`.  (When symlinks are involved
`bob/dob/..` might not be `bob`, of course.)  C++ header-unit
semantics are such that there is no need to resolve multiple ways of
spelling a particular header-unit to a unique CMI file.

Successful compilation of an interface is indicated with a subsequent:

`MODULE-COMPILED $module`

FIXME: do we need the module here?

request.  This indicates the CMI file has been written to disk, so
that any other compilations waiting on it may proceed.  Depending on
compiler implementation, the CMI may be written before the compilation
completes.  A single response:

`OK`

is expected.  Compilation failure can be inferred by lack of a
`MODULE-COMPILED` request.  It is presumed the builder can determine
this, as it is also responsible for launching and reaping the compiler
invocations themselves.

#### Importing

Importation, inculding that of header-units, uses:

`MODULE-IMPORT $module`

This responds with a `MODULE-CMI` of the same form as the
`MODULE-EXPORT` request's response.  Should the builder have to invoke
a compilation to produce the CMI, the response should be delayed until
that occurs.  If such a compilation fails, an error response should be
provided to the requestor &mdash; which will then presumably fail in
some manner.

#### Include Translation

Include translation can be determined with:

`INCLUDE-TRANSLATE $header`

The header name, `$header`, is the fully resolved header name, in the
above-mentioned unambigous filename form.  The response will be:

`INCLUDE-TEXT`

to indicate include translation should not occur (the usual textual
inclusion occurs).  Or:

`INCLUDE-IMPORT`

to indicate the include directive should be replaced by an import
declaration of the resolved header-unit.  Finally `MODULE-CMI`
response also indicates include translation should occur, and provides
the name of the CMI to read, this possibly elides a subsequent
`MODULE-IMPORT` request.

### GCC LTO Messages

These set of messages are used for GCC LTO jobserver integration with GNU Make

#### Invoke Command

A command can be invoked with the follow message:

`INVOKE $argv`

The expected response is:

`INVOKED $status`

The `$status` currently just returns a string `success` or `failed`. 
A successful invocation of the command is indicated with a return `success`.

## Building libCody

Libcody is written in C++11.  (It's a intended for compilers, so
there'd be a boostrapping problem if it used the latest and greatest.)

It uses the usual `configure`, `make`, `make check` & `make install`
sequence.  It does not support building in the source directory --
that just didn't drop out, and it's not how I build things (because,
again, for compilers).  Excitingly it uses my own `joust` test
harness, so you'll need to build and install that somewhere, if you
want the comfort of testing.

The following configure options are available, in addition to the usual set:

* `--enable-checking` Compile with assert-like checking.  Defaults to on.

* `--with-tooldir=DIR` Prepend `DIR` to `PATH` when building (`DIR`
  need not already include the trailing `/bin`, and the right things
  happen).  Use this if you need to point to non-standard tools that
  you usually don't have in your path.  This path is also used when
  the configure script searches for programs.

* `--with-toolinc=DIR`, `--with-toollib=DIR`, include path and library
  path variants of `--with-tooldir`.  If these are siblings of the
  tool bin directory, they'll be found automatically.

* `--with-compiler=NAME` Specify a particular compiler to use.
  Usually what configure finds is sufficiently usable.

* `--with-bugurl=URL` Override the bugreporting URL.  Do this if
  you're providing libcody as part of a package that /you/ are
  supporting.

* `--enable-maintainer-mode` Specify that rules to rebuild things like
  `configure` (with `autoconf`) should be enabled.  When not enabled,
  you'll get a message if these appear out of date, but that can
  happen naturally after an update or clone as `git`, in common with
  other VCs, doesn't preserve the relative ordering of file
  modifications.  You can use `make MAINTAINER=touch` to shut make up,
  if this occurs (or manually execute the `autoconf` and related
  commands).

When building, you can override the default optimization flags with
`CXXFLAGS=$flags`.  I often build a debuggable library with `make
CXXFLAGS=-g3`.

The `Makefile` will also parallelize according to the number of CPUs,
unless you specify explicitly with a `-j` option.  This is a little
clunky, as it's not possible to figure out inside the makefile whether
the user provided `-j`.  (Or at least I've not figured out how.)

## API

The library defines entities in the `::Cody` namespace.

There are 4 user-visible classes:

* `Packet`: Responses to requests are `Packets`.  These have a code,
  indicating the response kind, and a payload.

* `Client`: The compiler-end of a connection.  Requests may be made
  and responses are returned.

* `Server`: The builder-end of a connection.  Requests may be waited
  for, and responses made.  Builders that serve multiple concurrent
  connections and spawn compilations to resolve dependencies may need
  to derive from this class to provide response queuing.

* `Resolver`: The processing engine of the builder side.  User code is
  expected to derive from this class and provide virtual function
  overriders to affect the semantics of the resolver.

In addition there are a number of helpers to setup connections.

Logically the Client and the Server communicate via a sequential
channel.  The channel may be provided by:

* two pipes, with different file descriptors for reading and writing
  at each end.

* a socket, which will use the same file descriptor for reading and
  writing.  the socket can be created in a number of ways, including
  Unix domain and IPv6 TCP, for which helpers are provided.

* a direct, in-process, connection, using buffer swapping.

The communication channel is presumed reliable.

Refer to the (currently very sparse) doxygen-generated documentation
for details of the API.

## Examples

To create an in-process resolver, use the following boilerplate:

```
class MyResolver : Cody::Resolver { ... stuff here ... };

Cody::Client *MakeClient (char const *maybe_ident)
{
  auto *r = new MyResolver (...);
  auto *s = new Cody::Server (r);
  auto *c = new Cody::Client (s);

  auto t = c->ConnectRequest ("ME", maybe_ident);
  if (t.GetCode () == Cody::Client::TC_CONNECT)
    ;// Yay!
  else if (t.GetCode () == Cody::Client::TC_ERROR)
    report_error (t.GetString ());

  return c;
}

```

For a remotely connecting client:
```
Cody::Client *MakeClient ()
{
  char const *err = nullptr;
  int fd = OpenInet6 (char const **err, name, port);
  if (fd < 0)
    { ... error... return nullptr;}

  auto *c = new Cody::Client (fd);

  auto t = c->ConnectRequest ("ME", maybe_ident);
  if (t.GetCode () == Cody::Client::TC_CONNECT)
    ;// Yay!
  else if (t.GetCode () == Cody::Client::TC_ERROR)
    report_error (t.GetString ());

  return c;
}
```

# Future Directions

* Current Directory.  There is no mechanism to check the builder and
  the compiler have the same working directory.  Perhaps that should
  be addressed.

* Include path canonization and/or header file lookup.  This can be
  expensive, particularly with many `-I` options, due to the system
  calls.  Perhaps using a common resource would be cheaper?

* Generated header file lookup/construction.  This is essentially the
  same problem as importing a module, and build systems are crap at
  dealing with this.

* Link-time compilations.  Another place the compiler would like to
  ask the build system to do things.

* C++20 API entrypoints &mdash; std:string_view would be nice

* Exception-safety audit.  Exceptions are not used, but memory
  exhaustion could happen.  And perhaps user's resolver code employs
  exceptions?

<a name="1">1</a>: Or a small town in Wyoming

<a name="2">2</a>: This describes one common implementation technique.
The std itself doesn't require such serializations, but the ability to
create them is kind of the point.  Also, 'compiler' is used where we
mean any consumer of a module, and 'build system' where we mean any
producer of a module.

<a name="3">3</a>: Even when the builder is managing a distributed set
of compilations, the builder must have a mechanism to get source files
to, and object files from, the compilations.  That scheme can also
transfer the CMI files.
