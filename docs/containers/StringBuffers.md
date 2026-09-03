Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited  
License: MIT (see LICENSE file in repository root)  

File:   StringBuffers.md  
Author: Ritchie Brannan  
Date:   1 Apr 2026  

# StringBuffers

## Overview

This document defines string view, string buffer, and stable string
table utilities implemented in StringBuffers.hpp.

The layer provides byte-string storage and lookup utilities. Strings
are treated as byte sequences and may be represented as explicit-length
or zero-terminated forms.

No encoding validation or interpretation is performed.

The implementation is noexcept. Failure is reported via return values.
Accessors are fail-safe.

## Requirements and scope

- Requires C++17 or later.
- No exceptions are used.
- Strings are stored and manipulated as byte sequences.
- No wchar_t, char16_t, char32_t, or platform-specific wide encodings
  are used.
- Zero-terminated and explicit-length forms are supported.

Scope:

- Models byte-string storage and lookup only.
- Does not validate or interpret encoding.
- Does not provide locale-aware or Unicode-aware collation.
- Higher-level string meaning belongs in wrapper layers.

## String model

CStringView and CSimpleString:

- store pointer plus explicit length
- do not require zero termination

CStringBuffer:

- stores packed strings as:
      0 + payload bytes + 0
- offsets refer to the first payload byte
- payload length is not stored internally

Strings with embedded terminators require external length metadata.

Null storage means that no string is present. Non-null storage with zero
logical length represents a present but empty string and remains visible to
parser-facing code. Do not normalize these two states into one another.

## Stable string table model

CStableStrings:

- stores payload in a CStringBuffer
- associates each string with a StringRef:
  - payload offset
  - explicit length
- maintains:
  - stable IDs
  - ref-indices
  - sorted lexical order

ID value 0 is reserved as an invalid sentinel.

## Observation model

Accessors are fail-safe.

Invalid lookup operations return:

- empty views
- null pointers
- invalid sentinel values

check_integrity() validates compound stable-string state.

CStringBuffer and CStableStrings expose `storage_overlaps()` as a constant-time
storage-identity query. It reports whether a non-empty supplied byte range
overlaps used packed storage. It does not validate string heads, terminators,
substrings, encoding or any other string semantics.

## Comparison model

String comparison is:

- byte-wise
- length-aware

No encoding-aware normalisation or collation is performed.
