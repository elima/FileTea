/*
 * utils.js
 *
 * Useful, general purpose javascript functions.
 *
 * Copyright (C) 2011, Igalia S.L.
 *
 * Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

function humanizeFileSize (size) {
    var gig = Math.pow (10, 9);
    var meg = Math.pow (10,6);
    var kb = 1024;

    var d;
    if (size >= gig)
        d = [size / gig, "GB"];
    else if (size >= meg)
        d = [size / meg, "MB"];
    else if (size > kb)
        d = [size / kb, "KB"];
    else
        return size + " bytes";

    return Math.round (d[0] * 10) / 10 + " " + d[1];
}

if (exports == undefined)
    var exports = {};

exports.humanizeFileSize = humanizeFileSize;

define (exports);
