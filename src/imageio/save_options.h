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
    // set by the dialog
    std::string table;              ///< the ID its own table shares with the options' table
    bool        wide_gamut = false; ///< whether the image's primaries are other than BT.709's
    bool        rendition  = false; ///< whether it exports a rendition of the view, not the data

    // set by the options
    bool             hdr               = false; ///< whether the encoding keeps values above SDR white
    bool             records_primaries = false; ///< whether the file can say which primaries it is in
    bool             sdr_base          = false; ///< whether the file also needs the SDR rendition, for a gain map
    bool             hdr_base          = false; ///< with a gain map, whether the file's image is the HDR rendition
    TransferFunction tf                = TransferFunction::Linear; ///< the transfer function the samples are encoded in
    EncodingClip_    clip              = EncodingClip_None;        ///< how the samples clip what they are given
};

/// Whether an encoding keeps values above SDR white: an HDR transfer function, or floating-point samples.
inline bool is_hdr_encoding(const TransferFunction &tf, bool floating_point = false)
{ return floating_point || tf.type == TransferFunction::BT2100_PQ || tf.type == TransferFunction::BT2100_HLG; }

/// Tells \p ctx the encoding: samples in the transfer function \p tf, floating-point ones or integer ones, which clip.
/**
    \p icc_labeled says the file names \p tf with an ICC tone curve, as TIFF, JPEG 2000, WebP and HEIF/AVIF do every
    SDR one; a reader of the curve then goes no further than SDR white. PQ and HLG are named exactly, by a cicp tag or
    their code points.
*/
inline void set_encoding(SaveContext &ctx, const TransferFunction &tf, bool floating_point, bool icc_labeled = false)
{
    ctx.tf   = tf;
    ctx.clip = floating_point                        ? EncodingClip_None
               : icc_labeled && !is_hdr_encoding(tf) ? EncodingClip_ICCCurve
                                                     : EncodingClip_Codes;
}

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
    Each format's X_caveat() returns the one for its settings, from measurements of the readers, and its text names
    the version measured, so a reader that has since changed shows as such. On macOS, test_export_roundtrip.cpp and
    test_gainmap.cpp hold every caveat naming macOS to what ImageIO does, both ways; the other readers were checked
    by hand.
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
