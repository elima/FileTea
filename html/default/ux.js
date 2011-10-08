// UxManager
var UxManager = new Evd.Constructor ();
UxManager.prototype = new Evd.Object ();

Evd.Object.extend (UxManager.prototype, {

    _init: function (args) {
        var self = this;

        this._baseTitle = document.title;

        this._content = args.contentManager;

        function getTab (id, name, index) {
            var div = document.getElementById (id);
            if (! div) {
                div = document.createElement ("div");
                div.id = id;
                document.getElementById ("tabs").appendChild (div);
                self._tabs.tabs ("add", "#" + id, name, index);
            }

            return div;
        }

        this._content.addEventListener ("add",
            function (id, name, content, index) {
                var div = getTab (id, name, index);
                div.innerHTML = content;
            });
        this._content.addEventListener ("show",
            function (id, name) {
                document.title = name + " - " + self._baseTitle;
                self._tabs.tabs ("select", "#" + id);
            });
        this._content.addEventListener ("loading",
            function (id) {
                var div = document.getElementById (id);
                self._tabs.tabs ("select", "#" + id);
                div.innerHTML = '<div id="content-loading"></div>';
            });

        // confirm before navigating away from page
        window.onbeforeunload = function () {
            if (self._sharedFilesView.isEmpty ())
                return null;

            $("#dialog-confirm-close").dialog ({
                width: 640,
                height: 220,
                modal: true,
                position: "top",
                hide: "blind"
            });
            setTimeout (function () {
                            $( "#dialog-confirm-close" ).dialog("close");
                        }, 1);

            return "If you navigate away from this page, shared files will be removed and any active transfer will be interrupted. Are you sure?";
        };

        // shared-files view
        require (["sharedFilesView.js"],
            function (SharedFilesView) {
                self._sharedFilesView = new SharedFilesView ({
                    parentElement: $ ("#shared-files-list").get(0),
                    fileSources: Ft.files
                });

                self._sharedFilesView.addEventListener ("item-added",
                    function () {
                        self._content.open ("shared-files");
                    });
            });

        self._tabs = $ ("#tabs");

        self._tabs.tabs ({
            tabTemplate: "<li><a href='#{href}'>#{label}</a><span class='ui-icon ui-icon-close'>Remove Tab</span></li>",
            remove: function (event, ui) {
                self._content.invalidate (ui.panel.id);
            },
            select: window.location.hash
        });

        self._tabs.bind ("tabsselect",
            function (event, ui) {
                window.location.hash = ui.tab.hash;
            });

        $ ("#tabs span.ui-icon-close").live ("click",
            function (event, ui) {
                var index = $ ("li", self._tabs).index ($ (this).parent ());
                self._tabs.tabs ("remove", index);
            });

        self._content.start ();

        setTimeout (function () {
            $ ("#content").get (0).style.display = "block";
            self._fireEvent ("ready", []);
        }, 1);
    }
});
