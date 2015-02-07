/*
 * fileTea.js
 *
 * FileTea, low-friction file sharing <http://filetea.net>
 *
 * Copyright (C) 2015, Igalia S.L.
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

require ({
}, [
    "../common/contentManager.js",
    "../default/ux.js",
    "../common/fileTea.js"
], function (ContentManager, UxManager, Ft) {
    var content = new ContentManager ();

    content.add ("shared-files",
                 "Share files",
                 "../default/shared-files-view.html",
                 null,
                 content.Mode.STATIC);
    content.add ("transfers",
                 "Transfers",
                 null,
                 null,
                 content.Mode.STATIC);
    content.add ("privacy-policy",
                 "Privacy policy",
                 "../common/privacy-policy.html",
                 null,
                 content.Mode.DYNAMIC);

    content.setDefault ("shared-files");

    var ux = new UxManager ({
        contentManager: content,
        transferManager: Ft.transfers
    });
});
