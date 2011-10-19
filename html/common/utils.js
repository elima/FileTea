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

var KB = 1024;
var MB = KB * 1024;
var GB = MB * 1024;

var MIN = 60;
var HOUR = MIN * 60;
var DAY = HOUR * 24;

function humanizeFileSize (size) {
    var d;
    if (size >= GB)
        d = [size / GB, "GB"];
    else if (size >= MB)
        d = [size / MB, "MB"];
    else if (size > KB)
        d = [size / KB, "KB"];
    else
        return size + " bytes";

    return Math.round (d[0] * 10) / 10 + " " + d[1];
}

if (exports == undefined)
    var exports = {};

exports.humanizeFileSize = humanizeFileSize;

define (exports);
