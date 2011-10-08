// FragidNavigator
var FragidNavigator = new Evd.Constructor ();
FragidNavigator.prototype = new Evd.Object ();

Evd.Object.extend (FragidNavigator.prototype, {

    _init: function (args) {
        this._interval = 50;

        this._currentFragid = null;

        var self = this;
        this._checkFunc = function () {
                self.check ();
            };

        this._intervalId = null;

        if (args.autoStart !== false)
            this.start ();
    },

    _getFragmentId: function () {
        var hash = window.location.hash;
        if (hash)
            hash = hash.substr (1);
        return hash;
    },

    check: function () {
        var newFragid = this._getFragmentId ();
        if (newFragid != this._currentFragid) {
            var oldFragid = this._currentFragid;
            this._currentFragid = newFragid;
            this._fireEvent ("change", [oldFragid, newFragid]);
        }
    },

    start: function () {
        if (! this._intervalId) {
            this._intervalId = window.setInterval (this._checkFunc, this._interval);
            this.check ();
        }
    },

    stop: function () {
        if (this._intervalId) {
            window.removeInterval (this._intervalId);
            this._currentFragid = null;
        }
    },

    navigateTo: function (fragId) {
        window.location.hash = fragId;
    },

    getFragid: function () {
        return this._currentFragid;
    }
});

// ContentManager
var ContentManager = new Evd.Constructor ();
ContentManager.prototype = new Evd.Object ();

Evd.Object.extend (ContentManager.prototype, {

    Mode: {
        DYNAMIC:  0,
        STATIC:   1,
        VOLATILE: 2
    },

    _init: function (args) {
        this._states = args["stateManager"];
        this._fragidNav = args["fragidNav"];

        var self = this;
        this._fragidNav.addEventListener ("change",
            function (oldState, newState) {
                if (newState == "")
                    newState = self._default;

                self.open (newState);
            });

        this._contents = {};

        require (["../common/utils"], function (Utils) {
                     self._utils = Utils;
                 });
    },

    add: function (id, name, url, content, mode) {
        var c = {
            id: id,
            name: name,
            url: url,
            content: content,
            mode: mode !== undefined ? mode : this.Mode.DYNAMIC
        };

        this._contents[id] = c;
    },

    setDefault: function (id) {
        this._default = id;
    },

    open: function (id) {
        var self = this;

        var c = this._contents[id];
        if (! c) {
            this._fireEvent ("add", [id, "Download"]);
            this._fireEvent ("loading", [id]);

            // @TODO: remove this from here; it belongs to a DownloadView widget
            Ft.queryRemoteFile (id,
                function (info, error) {
                    if (error) {
                        self.add (id,
                                  "Download",
                                  "not-found-view.html",
                                  null,
                                  self.Mode.VOLATILE);
                        self.open (id);
                    }
                    else {
                        jQuery.ajax ({
                            url: "download-view.html",
                            success: function (data, statusText) {
                                self._fireEvent ("add", [id, "Download", data]);

                                $ ("#download-view-name").html (info.name);
                                if (! info.type)
                                    info.type = "unknown";
                                $ ("#download-view-type").html (info.type);
                                $ ("#download-view-size").html (self._utils.humanizeFileSize (info.size));
                                $ ("#download-view-url").get(0).href =  info.url;

                                self.add (id,
                                          "Download",
                                          "download-view.html",
                                          document.getElementById ("download-view").innerHTML,
                                          self.Mode.VOLATILE);
                                self.open (id);
                            }
                        });
                    }
            });

            return true;
        }

        if (c.content == null && c.mode != this.Mode.STATIC) {
            self._fireEvent ("add", [c.id, c.name]);
            self._fireEvent ("loading", [c.id]);

            jQuery.ajax ({
                url: c.url,
                success: function (data, statusText) {
                    c.content = data;
                    self._fireEvent ("add", [c.id, c.name, c.content]);
                    self._fireEvent ("show", [c.id, c.name]);
                }
            });
        }
        else {
            self._fireEvent ("show", [c.id, c.name, c.content]);
        }

        return true;
    },

    invalidate: function (id) {
        var c = this._contents[id];
        if (! c)
            return this;

        if (c.mode == this.Mode.DYNAMIC)
            c.content = null;
        else if (c.mode == this.Mode.VOLATILE)
            delete (this._contents[id]);

        return this;
    }
});

// FileSources
var FileSources = new Evd.Constructor ();
FileSources.prototype = new Evd.Object ();

var SourceStatus = {
    UNREGISTERED: 0,
    REGISTERING:  1,
    REGISTERED:   2
};

Evd.Object.extend (FileSources.prototype, {

    _init: function (args) {
        this._rpcFunc = args.rpcFunc;

        this._files = {};
        this._regFiles = {};
        this._fileCounter = 0;
    },

    add: function (files) {
        for (var i = 0; i < files.length; i++) {
            this._fileCounter++;

            var id = this._fileCounter.toString();
            var file = {
                id: id,
                name: files[i].name,
                type: files[i].type,
                size: files[i].size,
                blob: files[i],
                registered: false,
                status: SourceStatus.UNREGISTERED
            };

            this._files[id] = file;

            this._fireEvent ("new-file", [file]);
        }

        this._register ();
    },

    _getByStatus: function (status) {
        var result = [];
        for (var id in this._files)
        if (this._files[id].status == status) {
            result.push (this._files[id]);
        }
        return result;
    },

    _register: function () {
        var unregFiles = this._getByStatus (SourceStatus.UNREGISTERED);
        if (unregFiles.length == 0)
            return;

        var args = [];
        var file;
        for (var i = 0; i < unregFiles.length; i++) {
            file = unregFiles[i];
            args.push ([escape (file.name), file.type, file.size]);
            file.status = SourceStatus.REGISTERING;
        }

        var self = this;
        this._rpcFunc (function (rpc, error) {
            if (error) {
                alert (error);
                return;
            }

            rpc.callMethod ("addFileSources", args,
                function (result, error) {
                    if (error == null) {
                        self._onRegister (unregFiles, result);
                    }
                    else {
                        alert ("ERROR: " + error);
                    }
                });
        });
    },

    _onRegister: function (files, results) {
        for (i = 0; i < files.length; i++) {
            var result = results[i];
            files[i].remoteId = result[0];
            files[i].status = SourceStatus.REGISTERED;

            var a = document.createElement ("A");
            a.href = "/" + files[i].remoteId;
            files[i].url = a.href;

            this._regFiles[files[i].remoteId] = files[i];

            this._fireEvent ("registered", [files[i]]);
        }
    },

    collapse: function () {
        for (var i in this._regFiles) {
            var file = this._regFiles[i];

            file.status = SourceStatus.UNREGISTERED;
            file.url = null;

            this._fireEvent ("unregistered", [file]);
        }

        var self = this;
        setTimeout (function () {
            self._register ();
        }, 10);
    },

    remove: function (ids, abortTransfers) {
        var args = [];

        for (var i = 0; i < ids; i++) {
            var id = ids[i];
            if (this._files[id]) {
                var file = this._files[id];

                args.push (file.remoteId);

                delete (this._regFiles[file.remoteId]);
                delete (this._files[id]);
            }
        }

        this._rpcFunc (function (rpc, error) {
            if (error) {
                alert (error);
                return;
            }

            rpc.callMethod ("removeFileSources", [abortTransfers ? true : false, args],
                function (result, error) {
                    if (error)
                        alert ("ERROR: " + error);
                });
            });
    },

    getByRemoteId: function (id) {
        return this._regFiles[id];
    }
});

// FileTea global object
var Ft = new (function () {
    var self = this;

    this._transport = null;
    this._peer = null;
    this._jsonRpc = null;

    this._getRpc = function (callback) {
        if (self._jsonRpc != null)
            callback (self._jsonRpc, null);


        if (self._transport == null) {
            self._transport = new Evd.WebTransport ();

            self._transport.addEventListener ("new-peer",
                function (peer) {
                    self._peer = peer;

                    if (self._jsonRpc == null) {
                        require (["/transport/evdJsonrpc.js"],
                                 function () {
                                     self._setupRpc ();
                                     callback (self._jsonRpc, null);
                                 });
                    }
                    else {
                        // collapse!
                        self.files.collapse ();
                    }
                });

            self._transport.open ();
        }
    };

    this._setupRpc  = function () {
        var self = this;

        this._jsonRpc = new Evd.Jsonrpc ({
            transportWriteCb: function (jsonRpc, msg) {
                if (self._peer)
                    self._peer.sendText (msg);
                else {
                    // @TODO: No peer to send msg, backlog it please!
                    alert ("@TODO: No peer to send msg, add to backtog!");
                }
            }
        });

        this._jsonRpc.useTransport (this._transport);
    };

    // fragment identifier navigator
    this._fragidNav = new FragidNavigator({ autoStart: false });

    // content manager
    this.content = new ContentManager ({
        fragidNav: this._fragidNav
    });

    // user interface manager
    require (["ux.js"],
        function (UxManager) {
            self._ux = new UxManager ({
                contentManager: self.content
            });

            self._ux.addEventListener ("ready",
                function () {
                    self._fragidNav.start ();
                });
        });

    // file sources manager
    this.files = new FileSources ({
        rpcFunc: self._getRpc
    });

    // transfer manager
    this._transfers = null;
    this._onFileRegistered = function () {
        require (["../common/transfers.js"],
            function (TransferManager) {
                self._transfers = new TransferManager ({
                    rpcFunc: self._getRpc,
                    files: self.files
                });
            });

        this.removeEventListener ("registered", self._onFileRegistered);
    };
    this.files.addEventListener ("registered", this._onFileRegistered);

    this.queryRemoteFile = function (id, callback) {
        self._getRpc (function (rpc) {
            rpc.callMethod ("getFileSourceInfo", [id],
                function (result, error) {
                    if (error) {
                        callback (null, error);
                    }
                    else {
                        var info = {
                            name: unescape (result[0]),
                            type: result[1],
                            size: result[2]
                        };

                        var a = document.createElement ("A");
                        a.href = "/" + id + "/dl";

                        info.url = a.href;

                        callback (info, null);
                    }
                });
            });
    };

    window.addEventListener ("unload", function () {
        if (self._transport)
            self._transport.close (true);
    }, false);
}) ();
