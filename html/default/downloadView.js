/*
 * downloadView.js
 *
 * FileTea, low-friction file sharing <http://filetea.net>
 *
 * Copyright (C) 2011, Igalia S.L.
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

// DownloadView
var DownloadView = new Evd.Constructor ();
DownloadView.prototype = new Evd.Object ();

Evd.Object.extend (DownloadView.prototype, {

    _init: function (args) {
        this._parentElement = args.parentElement;

        var self = this;
        require (["../common/utils"],
            function (Utils) {
                self._utils = Utils;
            });
    },

    openFile: function (id, callback) {
        var self = this;

        this._fileId = id;

        Ft.queryRemoteFile (id,
            function (info, error) {
                if (error) {
                    jQuery.ajax ({
                        url: "not-found-view.html",
                        success: function (data, statusText) {
                            self._parentElement.innerHTML = data;

                            if (callback)
                                callback (false, new Error ("File not found"));
                        }
                    });
                }
                else {
                    jQuery.ajax ({
                        url: "download-view.html",
                        success: function (data, statusText) {
                            self._parentElement.innerHTML = data;

                            $ ("#" + id + " .download-view-name").html (info.name);

                            if (! info.type)
                                info.type = "unknown";

                            $ ("#" + id + " .download-view-type").html (info.type);
                            $ ("#" + id + " .download-view-size").html (self._utils.humanizeFileSize (info.size));

                            var urlEl = $ ("#" + id + " .download-view-url").get(0);
                            urlEl.href =  info.url;
                            urlEl._baseUrl = info.url;

                            urlEl.onclick = function () {
                                var peerId = Ft.getRemotePeerId ();
                                if (peerId)
                                    urlEl.href = urlEl._baseUrl + "?" + peerId;
                                return true;
                            };

                            if (callback)
                                callback (true, null);
                        }
                    });
                }
            });
    }
});

define (function () {
    return DownloadView;
});
