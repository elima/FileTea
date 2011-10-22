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
                                if (Ft._peer != null)
                                    urlEl.href = urlEl._baseUrl + "?" + Ft._peer.id;
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
