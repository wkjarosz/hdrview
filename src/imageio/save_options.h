//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "colorspace.h"

#include <string>

/// What the save dialog and a format's options tell each other about what is being saved.
struct SaveContext
{
    bool hdr        = false; ///< Set by the options: whether the encoding keeps values above SDR white
    std::string table;              ///< Set by the dialog: the ID its own table shares with the options' table
    bool wide_gamut        = false; ///< Set by the dialog: whether the image's primaries are other than BT.709's
    bool records_primaries = false; ///< Set by the options: whether the file can say which primaries it is in
    bool sdr_base = false; ///< Set by the options: whether the file's base is an SDR rendition, as a gain map's is
};

/// Whether an encoding keeps values above SDR white: an HDR transfer function, or floating-point samples.
inline bool is_hdr_encoding(const TransferFunction &tf, bool floating_point = false)
{ return floating_point || tf.type == TransferFunction::BT2100_PQ || tf.type == TransferFunction::BT2100_HLG; }

/// Returns \p tf, the one transfer function \p format can label, warning when \p asked was another.
TransferFunction labeled_tf(const TransferFunction &asked, TransferFunction::Type_ tf, const char *format);

/// Applications known to misread some files HDRView writes, which the save dialogs warn of.
enum Misreader_ : unsigned
{
    Misreader_macOS    = 1u << 0, ///< Apple's ImageIO, so Preview, Safari and every other macOS application
    Misreader_Chromium = 1u << 1, ///< Chrome, Edge and the other Chromium browsers
    Misreader_Firefox  = 1u << 2, ///< Firefox
};

/// A warning that some applications misread a file saved so, naming them and what they do instead.
/**
    Each format's X_caveat() returns the one for its settings, from measurements of the readers. On macOS,
    test_export_roundtrip.cpp holds every caveat naming macOS to what ImageIO does, both ways; the other readers
    were checked by hand, at the versions each X_caveat()'s comment gives.
*/
struct Caveat
{
    unsigned    readers = 0;       ///< the Misreader_ flags of the applications
    const char *text    = nullptr; ///< the warning, a sentence or two
};

/// Draws \p caveat's warning below a format's options, if it has one.
void caveat_entry(const Caveat &caveat);

/// Says, in place of a format's options, that it has none.
void no_options_entry();

/// Begins a save dialog's options table, as ImGui::PE::Begin() does; end it with ImGui::PE::End().
/**
    Every format's table and the rendition's share \p ctx's ID, so their columns' widths stay in step. The first
    column starts wide enough for the HEIF/AVIF options' Advanced tree node, a level in under Encoder, but not for
    the rows inside it. The widths are not saved, so each dialog starts from these rather than a width once dragged.
*/
bool begin_options_table(const SaveContext &ctx);

/// A table row whose button resets a format's options to their defaults; returns whether it was pressed.
bool reset_entry();
