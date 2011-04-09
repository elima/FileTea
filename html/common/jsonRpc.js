define (["/transport/evdWebTransport.js",
         "/transport/evdJsonrpc.js"],
    function () {
        // implement singleton
        if (window["net.filetea.ui.jsonrpc"])
            return window["net.filetea.ui.jsonrpc"];

        var obj;
        window["net.filetea.ui.jsonrpc"] = obj = new Evd.Constructor ();
        obj.prototype = new Evd.Object ();
        Evd.Object.extend (obj.prototype, {
            _init: function (args) {
                var self = this;

                this.rpc = null;

                this._transport = new Evd.WebTransport ();

                this.rpc = new Evd.Jsonrpc ();
                this.rpc.useTransport (this._transport);

                this._transport.addEventListener ("new-peer",
                    function (peer) {
                        self.peer = peer;
                        self._fireEvent ("connected", []);
                    });

                this._transport.open ();
            },

            close: function () {
                this.peer.close ();
            }
        });

        return new obj ();
    });
