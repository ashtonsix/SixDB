#pragma once
#include <ikea/hashtable.h>

// A concrete private prototype instantiation, never an opaque production layout.
using PrototypeTable = ikea::hashtable::table<24, 10, 0>;
