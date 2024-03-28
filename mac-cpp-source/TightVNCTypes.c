/****************************************************************************
 *   MiniVNC (c) 2022-2024 Marcio Teixeira                                  *
 *                                                                          *
 *   This program is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as published by   *
 *   the Free Software Foundation, either version 3 of the License, or      *
 *   (at your option) any later version.                                    *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   To view a copy of the GNU General Public License, go to the following  *
 *   location: <http://www.gnu.org/licenses/>.                              *
 ****************************************************************************/


#include "TightVNCTypes.h"

/*
 * Reference:
 *   https://stackoverflow.com/questions/70720527/specification-of-client-uploads-in-tightvnc-rfb
 *   https://vncdotool.readthedocs.io/en/0.8.0/rfbproto.html#serverinit
 *   https://github.com/TurboVNC/tightvnc/blob/a235bae328c12fd1c3aed6f3f034a37a6ffbbd22/vnc_unixsrc/include/rfbproto.h#L420
 */

struct TightVNCServerAuthCaps tightAuthCaps = {
    0, // numberOfTunnelTypes
    2, // numberOfAuthTypes
    {
        {   1,         "STDV", "NOAUTH__"},
        {   2,         "STDV", "VNCAUTH_"}
    }
};

struct TightVNCServerInitCaps tightInitCaps = {
    7, // numberOfServerMesg
    6, // numberOfClientMesg
    3, // numberOfEncodings
    0, // padding

    {
        /***** Server Messages *****/

        // Legacy File Transfer Messages

        //{  130,        "TGHT", "FTS_LSDT"}, // File list data
        //{  131,        "TGHT", "FTS_DNDT"}, // File download data
        //{  132,        "TGHT", "FTS_UPCN"}, // File upload cancel
        //{  133,        "TGHT", "FTS_DNFL"}, // File download failed

        // New TightVNC File Transfer Messages:

        //{  0xFC000101, "TGHT", "FTSCSRLY"}, // Compression support reply
        {  0xFC000103, "TGHT", "FTSFLRLY"}, // File list reply
        //{  0xFC000105, "TGHT", "FTSM5RLY"}, // MD5 reply

        {  0xFC000107, "TGHT", "FTSFURLY"}, // Upload start reply
        {  0xFC000109, "TGHT", "FTSUDRLY"}, // Upload data reply
        {  0xFC00010B, "TGHT", "FTSUERLY"}, // Upload end reply

        //{  0xFC00010D, "TGHT", "FTSFDRLY"}, // Download start reply
        //{  0xFC00010F, "TGHT", "FTSDDRLY"}, // Download data reply
        //{  0xFC000110, "TGHT", "FTSDERLY"}, // Download end reply

        {  0xFC000112, "TGHT", "FTSMDRLY"}, // Mkdir reply
        {  0xFC000114, "TGHT", "FTSFTRLY"}, // File remove reply
        //{  0xFC000116, "TGHT", "FTSFMRLY"}, // File rename reply
        //{  0xFC000118, "TGHT", "FTSDSRLY"}, // Dir size reply

        {  0xFC000119, "TGHT", "FTLRFRLY"}, // Last request failed reply

        /***** Client Messages *****/

        // Legacy File Transfer Messages

        //{  130,        "TGHT", "FTC_LSRQ"}, // File list request
        //{  131,        "TGHT", "FTC_DNRQ"}, // File download request
        //{  132,        "TGHT", "FTC_UPRQ"}, // File upload request
        //{  133,        "TGHT", "FTC_UPDT"}, // File upload data
        //{  134,        "TGHT", "FTC_DNCN"}, // File download cancel
        //{  135,        "TGHT", "FTC_UPFL"}, // File upload failed
        //{  136,        "TGHT", "FTC_FCDR"}, // File create directory request

        // New TightVNC File Transfer Messages:

        //{  0xFC000100, "TGHT", "FTCCSRST"}, // Compression support request
        {  0xFC000102, "TGHT", "FTCFLRST"}, // File list request
        //{  0xFC000104, "TGHT", "FTCM5RST"}, // MD5 request

        {  0xFC000106, "TGHT", "FTCFURST"}, // Upload start request
        {  0xFC000108, "TGHT", "FTCUDRST"}, // Upload data request
        {  0xFC00010A, "TGHT", "FTCUERST"}, // Upload end request

        //{  0xFC00010C, "TGHT", "FTCFDRST"}, // Download start request
        //{  0xFC00010E, "TGHT", "FTCDDRST"}, // Download data request

        {  0xFC000111, "TGHT", "FTCMDRST"}, // Mkdir request (required for upload/download)
        {  0xFC000113, "TGHT", "FTCFRRST"}, // File remove request
        //{  0xFC000115, "TGHT", "FTCFMRST"}, // File rename request
        //{  0xFC000117, "TGHT", "FTCDSRST"}, // Dir size request (required for download)

        /***** Encodings *****/

        {    5,        "STDV", "HEXTILE_"}, // Hextile encoding
        {   16,        "STDV", "ZRLE____"}, // ZRLE encoding
        { -239,        "TGHT", "RCHCURSR"}  // Rich cursor pseudo-encoding
    }
};