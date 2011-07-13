// TransferManager
var TransferManager = new Evd.Constructor ();
TransferManager.prototype = new Evd.Object ();

Evd.Object.extend (TransferManager.prototype, {

    _init: function (args) {
        this._files = args.files;

        this._rpc = null;

        var self = this;
        args.rpcFunc (function (rpc, error) {
            if (error) {
                alert (error);
                return;
            }

            self._rpc = rpc;

            self._rpc.registerMethod ("fileTransferNew",
                function (rpc, params, invocation, context) {
                    self._onNewTransferRequest (rpc, params, invocation, context);
                }
            );
        });
    },

    _onNewTransferRequest: function (rpc, params, invocation, context) {
        var fileId = params[0];
        var transferId = params[1];

        var file = this._files.getByRemoteId (fileId);
        if (! file) {
            this._rpc.respondError (invocation, "File not found", context);
            return;
        }

        this._transferFile (file, transferId);
    },

    _transferFile: function  (file, transferId) {
        // upload using XMLHttpRequest
        // @TODO: check if this is supported by browser
        var xhr = new XMLHttpRequest ();
        var url = "/" + transferId;
        xhr.open ("PUT", url, true);

        self = this;
        xhr.onreadystatechange = function () {
            if (! this.readyState == 4)
                return;

            // @TODO: check for errors and notify
        };

        xhr.send (file.blob);
    }
});

define (function () {
    return TransferManager;
});
