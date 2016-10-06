/*
 * transfersView.js
 *
 * FileTea, low-friction file sharing <http://filetea.net>
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

define ([
    "/transport/evdWebTransport.js",
    "./fileTea.js"
], function (Evd, Ft) {

    // TransfersView
    var TransfersView = new Evd.Constructor ();
    TransfersView.prototype = new Evd.Object ();

    Evd.Object.extend (TransfersView.prototype, {

        _init: function (args) {
            this._parentElement = args.parentElement;
            this._transfers = args.transferManager;

            var self = this;

            this._items = {};
            this._dlItemCount = 0;
            this._ulItemCount = 0;

            this._dlList = document.getElementById ("downloads-list");
            this._ulList = document.getElementById ("uploads-list");

            this._dlListEmptyNote = this._newContainer (this._dlList,
                                                        "No transfers",
                                                        "list-empty-note");
            this._ulListEmptyNote = this._newContainer (this._ulList,
                                                        "No transfers",
                                                        "list-empty-note");

            require (["./utils"], function (Utils) {
                self._utils = Utils;
            });

            this._transfers.addEventListener ("transfer-started",
                function (transfer) {
                    self.add (transfer);
                });

            this._transfers.addEventListener ("transfer-status",
                function (transfer) {
                    self._onTransferStatus (transfer);
                });

            this._transfers.addEventListener ("transfer-finished",
                function (transfer) {
                    self._onTransferFinished (transfer);
                });

            this._classByStatus = [
                "not-started",
                "active",
                "paused",
                "completed",
                "aborted",
                "aborted"
            ];
        },

        _newContainer: function (parent, inner, className) {
            var el = document.createElement ("div");
            el.innerHTML = inner;
            el.className = className;
            parent.appendChild (el);
            return el;
        },

        _getProgressLabel: function (item, transfer) {
            return this._utils.humanizeFileSize (transfer.transferred) + " / " + item.totalSizeSt;
        },

        add: function (transfer) {
            var self = this;

            var id = transfer.id;
            var name = transfer.fileName;

            var className = "transfer-item " + this._classByStatus[transfer.status];
            var item = this._newContainer (this._parentElement, null, className);
            item.isDownload = transfer.isDownload;

            item.totalSizeSt = this._utils.humanizeFileSize (transfer.fileSize);

            item.thumbEl = document.createElement ("img");
            item.thumbEl.className = "transfer-file-thumb";
            item.thumbEl.src = "./mime-type-icon-default.png";
            item.appendChild (item.thumbEl);

            item.nameEl = this._newContainer (item, name, "transfer-file-name");

            item.progLabelEl = this._newContainer (item,
                                                   this._getProgressLabel (item, transfer),
                                                   "transfer-progress-label");

            item.progBarEl = this._newContainer (item,
                                                 "",
                                                 "transfer-progress-bar");
            item.progBarInnerEl = this._newContainer (item.progBarEl,
                                                      "&nbsp;",
                                                      "transfer-progress-bar-inner");
            item.progBarLabelEl = this._newContainer (item.progBarEl,
                                                      "&nbsp;",
                                                      "transfer-progress-bar-label");

            item.timeLabelEl = this._newContainer (item,
                                                   "",
                                                   "transfer-time-label");

            item.bwLabelEl = this._newContainer (item, "", "transfer-bw-label");

            item.delEl = this._newContainer (item, "", "transfer-cancel-btn");
            item.delEl.title = "Cancel transfer of '" + name + "'";
            item.delEl.onclick = function (e) {
                self.cancel (id);
            };

            this._items[id] = item;

            if (transfer.isDownload) {
                if (this._dlItemCount == 0)
                    this._dlList.removeChild (this._dlListEmptyNote);

                this._dlItemCount++;
                this._dlList.insertBefore (item, this._dlList.childNodes.item (0));
            }
            else {
                if (this._ulItemCount == 0)
                    this._ulList.removeChild (this._ulListEmptyNote);

                this._ulItemCount++;
                this._ulList.insertBefore (item, this._ulList.childNodes.item (0));
            }

            this._onTransferStatus (transfer);

            this._fireEvent ("item-added", []);

            this._fireEvent ("have-updates", []);
        },

        isEmpty: function () {
            return this._itemCount == 0;
        },

        cancel: function (id) {
            var item = this._items[id];
            if (! item)
                return;

            var self = this;
            if (item.status == Ft.TransferStatus.ACTIVE) {
                $ ("#transfer-list-confirm-cancel").dialog({
                    modal: true,
                    title: "Cancel transfer",
                    buttons: {
                        "Yes": function () {
                            self._transfers.cancel ([id]);

                            $ (this).dialog ("close");
                        },
                        "No": function () {
                            $ (this).dialog ("close");
                        }
                    }
                });
            }
            else {
                delete (this._items[id]);

                item.parentNode.removeChild (item);

                if (item.isDownload) {
                    this._dlItemCount--;
                    if (this._dlItemCount == 0)
                        this._dlList.appendChild (this._dlListEmptyNote);
                }
                else {
                    this._ulItemCount--;
                    if (this._ulItemCount == 0)
                        this._ulList.appendChild (this._ulListEmptyNote);
                }
            }
        },

        _onTransferStatus: function (transfer) {
            var item = this._items[transfer.id];
            if (! item)
                return;

            item.status = transfer.status;

            var progPercent = Math.floor ((transfer.transferred / transfer.fileSize) * 100 * 100) / 100;
            item.progBarInnerEl.style.width = progPercent + "%";
            item.progBarLabelEl.innerHTML = progPercent + "%";

            if (transfer.bandwidth > 0) {
                item.timeLabelEl.visibility = "visible";
                var time = (transfer.fileSize - transfer.transferred) / (transfer.bandwidth * 1024);
                item.timeLabelEl.innerHTML = this._utils.humanizeTime (time);
            }
            else {
                item.timeLabelEl.visibility = "hidden";
            }

            item.progLabelEl.innerHTML = this._getProgressLabel (item, transfer);

            item.className = "transfer-item " + this._classByStatus[transfer.status];

            item.bwLabelEl.innerHTML = this._utils.humanizeFileSize (Math.round (transfer.bandwidth * 1024)) + "/s";
        },

        _onTransferFinished: function (transfer) {
            $ ("#transfer-list-confirm-cancel").dialog ("close");

            this._onTransferStatus (transfer);

            var item = this._items[transfer.id];
            if (! item)
                return;

            item.delEl.title = "Remove from list";

            this._fireEvent ("have-updates", []);
        }
    });

    return TransfersView;
});
