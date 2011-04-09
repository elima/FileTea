// FragidNavigator

var FragidNavigator = new Evd.Constructor ();
FragidNavigator.prototype = new Evd.Object ();

Evd.Object.extend (FragidNavigator.prototype, {

    _init: function (args) {
        this._interval = 50;

        this._currentFragid = null;

        var self = this;
        this._checkFunc = function () {
                self._checkFragid ();
            };

        this.start ();
    },

    _getFragmentId: function () {
        var tokens = window.location.toString().match(/.*#(.*)/);
        if (tokens && tokens.length > 0)
            return tokens[1];
        else
            return null;
    },

    _checkFragid: function () {
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
            this._checkFragid ();
        }
    },

    stop: function () {
        if (this._intervalId) {
            window.removeInterval (this._intervalId);
            this._currentFragid = null;
        }
    },

    navigateTo: function (fragId) {
        window.location = "#" + fragId;
    }
});
