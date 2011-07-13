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

// StateManager
var StateManager = new Evd.Constructor ();
StateManager.prototype = new Evd.Object ();

StateManager.DEFAULT_STATE = "_default_";

Evd.Object.extend (StateManager.prototype, {

    _init: function (args) {
        var self = this;

        this._states = {};

        this._defaultState = args.defaultState || StateManager.DEFAULT_STATE;

        this._fragidNav = new FragidNavigator({ autoStart: false });
        this._fragidNav.addEventListener ("change",
            function (oldState, newState) {
                self._onStateChanged (oldState, newState);
            });
    },

    _onStateChanged: function (oldState, newState) {
        if (newState == "")
            newState = this._defaultState;

        var oldS = this._states[oldState];
        if (oldS && oldS.exitFunc)
            oldS.enterFunc (oldState);

        var newS = this._states[newState];
        if (newS && newS.enterFunc)
            newS.enterFunc (newState);
    },

    add: function (id, enterFunc, exitFunc) {
        var s = {
            id: id,
            enterFunc: enterFunc,
            exitFunc: exitFunc
        };

        this._states[id] = s;
    },

    start: function () {
        this._fragidNav.start ();
    },

    setDefault: function (state) {
        this._defaultState = state.toString();
    }
});

// ContentManager
var ContentManager = new Evd.Constructor ();
ContentManager.prototype = new Evd.Object ();

Evd.Object.extend (ContentManager.prototype, {

    _init: function (args) {
        this._states = args["stateManager"];

        this._contents = {};
    },

    add: function (id, name, desc, content, options) {
        if (! options)
            options = {};

        var c = {
            id: id,
            name: name,
            desc: desc,
            content: content,
            options: options
        };

        this._contents[id] = c;

        var self = this;
        this._states.add (id,
            function (id) {
                self.open (id);
            });
    },

    setDefault: function (id) {
        this._states.setDefault (id);
    },

    open: function (id) {
        var c = this._contents[id];
        if (! c)
            return false;

        var self = this;
        if (c.content == null && c.options["static"] !== true) {
            jQuery.ajax ({
                url: id + ".html",
                success: function (data, statusText) {
                    c.content = data;
                    self._fireEvent ("add", [c.id, c.name, c.desc, c.content]);
                    self._fireEvent ("show", [c.id, c.name, c.desc, c.content]);
                }
            });
        }
        else {
            self._fireEvent ("show", [c.id, c.name, c.desc, c.content]);
        }

        return true;
    },

    invalidate: function (id) {
        var c = this._contents[id];
        if (! c)
            return this;

        c.content = null;

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
            args.push ([file.name, file.type, file.size]);
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

    // state manager
    this._states = new StateManager ();

    // content manager
    this.content = new ContentManager ({
        stateManager: this._states
    });

    // user interface manager
    require (["ux.js"],
        function (UxManager) {
            self._ux = new UxManager ({
                contentManager: self.content
            });

            self._ux.addEventListener ("ready",
                function () {
                    self._states.start ();
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

    window.addEventListener ("unload", function () {
        if (self._transport)
            self._transport.close (true);
    }, false);
}) ();
