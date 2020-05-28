// CODYlib		-*- mode:c++ -*-
// Copyright (C) 2020 Nathan Sidwell, nathan@acm.org
// License: LGPL v3.0 or later

// Test message encoding, both string quoting and continuation lines

// RUN: $subdir$stem |& ezio $src
// RUN-END:
// CHECK-NEXT: ^bob 'frob dob''\n\80\\'$
// CHECK-NEXT: ^2 \$
// CHECK-NEXT: ^3$
// CHECK-NEXT: $EOF

// Cody
#include "cody.hh"
// C
#include <cstdlib>

using namespace Cody;

int main (int, char *[])
{
  MessageBuffer writer;

  writer.BeginMessage ();
  writer.BeginLine ();
  writer.AppendWord ("bob");
  writer.AppendWord ("frob dob", true);
  writer.Append ("\n\x80\\", true);
  writer.EndLine ();
  writer.EndMessage ();
  while (int err = writer.Write (2))
    {
      if (err != EAGAIN && err != EINTR)
	break;
    }

  writer.BeginMessage ();
  writer.BeginLine ();
  writer.Append ("2", true);
  writer.EndLine ();
  writer.BeginLine ();
  writer.Append ("3", true);
  writer.EndLine ();
  writer.EndMessage ();
  while (int err = writer.Write (2))
    {
      if (err != EAGAIN && err != EINTR)
	break;
    }

  return 0;
}
