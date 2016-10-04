/*
 * utils.js
 *
 * Useful, general purpose javascript functions.
 *
 * Copyright (C) 2011-2016, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License at http://www.gnu.org/licenses/agpl.html
 * for more details.
 */

'use strict';

define ([
], function () {
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

    function humanizeTime (seconds) {
        var st = "";

        if (seconds > DAY) {
            st += Math.floor (seconds / DAY) + "d ";
            seconds = seconds % DAY;
        }

        if (seconds > HOUR) {
            st += Math.floor (seconds / HOUR) + "h ";
            seconds = seconds % HOUR;
        }

        if (seconds > MIN) {
            st += Math.floor (seconds / MIN) + "m ";
            seconds = seconds % MIN;
        }

        st += Math.ceil (seconds) + "s";

        return st;
    }

    return {
        humanizeFileSize: humanizeFileSize,
        humanizeTime: humanizeTime
    };
});
