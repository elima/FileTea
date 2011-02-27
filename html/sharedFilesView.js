// SharedFilesView class

var SharedFilesView = new Evd.Constructor ();
SharedFilesView.prototype = new Evd.Object ();

Evd.Object.extend (SharedFilesView.prototype, {

    _init: function (args) {
        this._parentElement = args.parentElement;
        this._items = {};
        this._itemCount = 0;
        this._emptyNoticeElement = this._parentElement.childNodes.item (0);
    },

    _newContainer: function (parent, inner, className) {
        var el = document.createElement ("div");
        el.innerHTML = inner;
        el.className = className;
        parent.appendChild (el);
        return el;
    },

    _createThumbnail: function (file, imgElement) {
        var reader = new FileReader();
        reader.onload = function (e) {
            imgElement.src = e.target.result;
        };
        reader.readAsDataURL (file);
    },

    add: function (id, file, url) {
        var self = this;

        var name = file.name;
        var type = file.type;
        var size = file.size;

        var item = this._newContainer (this._parentElement,
                                       null,
                                       "shared-file-item");

        item.thumbEl = document.createElement ("img");
        item.thumbEl.className = "shared-file-thumb";
        if (type.indexOf ("image/") == 0 && size < 1024000)
            this._createThumbnail (file, item.thumbEl);
        else
            item.thumbEl.src = "/mime-type-icon-default.png";
        item.appendChild (item.thumbEl);

        item.nameEl = this._newContainer (item, name, "shared-file-name");
        item.infoEl = this._newContainer (item, size + " - " + type, "shared-file-info");

        var urlHtml = "<input type='text' readonly='true' value='"+url+"'/>";
        item.urlEl = this._newContainer (item, urlHtml, "shared-file-url");
        item.onclick = function (e) {
            this.urlEl.childNodes.item (0).select ();
        };
        item.urlEl.title = "Copy this link and send it to share the file";

        item.delEl = document.createElement ("img");
        item.delEl.src = "/delete.png";
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
        item.urlEl.childNodes.item (0).select ();

        this._fireEvent ("source-added", [id, file, url]);
    },

    remove: function (id) {
        var item = this._items[id];
        if (item) {
            delete (this._items[id]);
            this._itemCount--;

            item.parentNode.removeChild (item);
            this._fireEvent ("source-removed", [[id]]);

            if (this._itemCount == 0)
                this._parentElement.appendChild (this._emptyNoticeElement);
        }
    }
});
