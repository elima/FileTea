// SharedFilesView
var SharedFilesView = new Evd.Constructor ();
SharedFilesView.prototype = new Evd.Object ();

Evd.Object.extend (SharedFilesView.prototype, {

    _init: function (args) {
        this._parentElement = args.parentElement;
        this._files = args.fileSources;

        this._items = {};
        this._itemCount = 0;
        this._emptyNoticeElement = document.getElementById ("shared-files-list-empty-notice");

        var self = this;

       this._files.addEventListener ("new-file",
            function (file) {
                self.add (file);
            });

        this._files.addEventListener ("registered",
            function (file) {
                self.setRegistered (file.id, file.url);
            });

        this._files.addEventListener ("unregistered",
            function (file) {
                self.setUnregistered (file.id);
            });

        $ ("#shared-files-selector").get (0).onchange =
            function () {
                self._addButtonOnChange ();
            };

        $ ("#share-files-btn").button ();

        // @TODO: check if browser supports file drag-and-drop
        this._setupFileDropZone (window.document);

        require (["../common/utils"], function (Utils) {
                     self._utils = Utils;
                 });
    },

    _addButtonOnChange: function () {
        var files = $ ("#shared-files-selector").get(0).files;
        this._files.add (files);
    },

    _newContainer: function (parent, inner, className) {
        var el = document.createElement ("div");
        el.innerHTML = inner;
        el.className = className;
        parent.appendChild (el);
        return el;
    },

    _createThumbnail: function (file, imgElement) {
        try {
            var reader = new FileReader();
            reader.onload = function (e) {
                imgElement.src = e.target.result;
            };
            reader.readAsDataURL (file);
        }
        catch (e) {}
    },

    add: function (file) {
        var self = this;

        var id = file.id;
        var name = file.name;
        var type = file.type != "" ? file.type : "unknown";
        var size = this._utils.humanizeFileSize (file.size);

        var item = this._newContainer (this._parentElement,
                                       null,
                                       "shared-file-item");

        item.thumbEl = document.createElement ("img");
        item.thumbEl.className = "shared-file-thumb";
        if (type.indexOf ("image/") == 0 && size < 1024000)
            this._createThumbnail (file, item.thumbEl);
        else
            item.thumbEl.src = "../common/mime-type-icon-default.png";
        item.appendChild (item.thumbEl);

        item.nameEl = this._newContainer (item, name, "shared-file-name");
        item.infoEl = this._newContainer (item, size + " - " + type, "shared-file-info");

        item.urlEl = this._newContainer (item, "", "shared-file-url");
        if (file.status == SourceStatus.REGISTERED) {
            var url = file.url || "";
            item.urlEl.innerHTML = "<input type='text' readonly='true' value='"+url+"'/>";
            item.onclick = function (e) {
                this.urlEl.childNodes.item (0).select ();
            };
            item.urlEl.title = "Copy this link and send it to share the file";
            item.urlEl.childNodes.item (0).select ();
        }
        else {
            item.urlEl.innerHTML = "<img src='../common/loading.gif'>";
        }

        item.delEl = document.createElement ("img");
        item.delEl.src = "../common/delete.png";
        item.delEl.className = "shared-file-del-btn";
        item.appendChild (item.delEl);
        item.delEl.title = "Unshare file " + name;
        item.delEl.onclick = function (e) {
            self.remove (id);
        };

        if (this._itemCount == 0)
            this._parentElement.removeChild (this._emptyNoticeElement);

        this._items[id] = item;
        this._itemCount++;

        this._parentElement.appendChild (item);

        this._fireEvent ("item-added", []);
    },

    isEmpty: function () {
        return this._itemCount == 0;
    },

    remove: function (id) {
        var item = this._items[id];
        if (! item)
            return;

        delete (this._items[id]);
        this._itemCount--;

        item.parentNode.removeChild (item);

        this._files.remove ([id]);

        if (this._itemCount == 0)
            this._parentElement.appendChild (this._emptyNoticeElement);
    },

    setRegistered: function (id, url) {
        var item = this._items[id];
        if (! item)
            return;

        item.urlEl.innerHTML = "<input type='text' readonly='true' value='"+url+"'/>";
        item.onclick = function (e) {
            this.urlEl.childNodes.item (0).select ();
        };
        item.urlEl.title = "Copy this link and send it to share the file";
        item.urlEl.childNodes.item (0).select ();
    },

    setUnregistered: function (id) {
        var item = this._items[id];
        if (! item)
            return;

        item.urlEl.innerHTML = "<img src='../common/loading.gif'>";
    },

    _setupFileDropZone: function (element) {
        var dropbox = element;

        dropbox.addEventListener ("dragenter", dragenter, false);
        dropbox.addEventListener ("dragover", dragover, false);
        dropbox.addEventListener ("drop", drop, false);

        function dragenter(e) {
            e.stopPropagation();
            e.preventDefault();
        }

        function dragover(e) {
            e.stopPropagation();
            e.preventDefault();
        }

        var self = this;
        function drop(e) {
            e.stopPropagation();
            e.preventDefault();

            var dt = e.dataTransfer;
            var files = dt.files;

            self._files.add (files);
        }
    }
});

define (function () {
    return SharedFilesView;
});
