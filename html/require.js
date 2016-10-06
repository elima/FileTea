/*
 * require.js
 *
 * Lite implementation of require.js
 *
 * Copyright (C) 2016, Igalia S.L.
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

'use strict';

(function (global) {
    var inWorker = false;

    var head, anchor;
    if (global["document"]) {
        head = document.querySelector ("head");
        anchor = document.createElement ("a");
    }
    else {
        inWorker = true;
    }

    var defBaseUrl, localUrl;

    var deferredReqs = [],
        lastReq,
        curCtx,
        defCtx,
        modules = {},
        reqCount = 0;

    var ModStatus = {
        NONE:    0,
        LOADING: 1,
        READY:   2
    };

    function getAbsUrl (url) {
        if (! inWorker) {
            anchor.setAttribute ("href", url);
            return anchor.href;
        }
        else {
            return url;
        }
    }

    function getBaseUrl (url) {
        var tokens = url.split ("/");
        tokens.length--;
        return tokens.join ("/") + "/";
    }

    function getRootUrl (url) {
        if (! inWorker) {
            anchor.setAttribute ("href", url);
            anchor.setAttribute ("pathname", "/");
            return anchor.href;
        }
        else {
            return url;
        }
    }

    function getContextForUrl (url) {
        url = getAbsUrl (url);

        return {
            baseUrl: getBaseUrl (url),
            rootUrl: getRootUrl (url)
        };
    }

    function resolveAbsUrl (url, ctx) {
        url = url.replace(/^\s+|\s+$/g,'');

        if (url.indexOf ("http://") == 0 || url.indexOf ("https://") == 0)
            return url;
        else if (url.charAt (0) == "/")
            return getAbsUrl (url);
        else if (url.charAt (0) == ".")
            return getAbsUrl (ctx.baseUrl + url);
        else
            return defBaseUrl + url;
    }

    function getResourceName (url) {
        var tokens = url.split ("/");
        var name = tokens[tokens.length-1];
        if (name.indexOf (".") < 0)
            name += ".js";
        return name;
    }

    function completeLoad (mod) {
        if (! mod.value)
            return;

        mod.status = ModStatus.READY;

        for (var i=0; i<mod.children.length; i++) {
            var obj = mod.children[i];
            var req = obj.req;
            req.resources[obj.index] = mod.value;
            req.resolved++;

            if (req.resolved == req.deps.length) {
                depsReady (req);
            }
        }

        mod.children = [];
    }

    function moduleOnLoad (url) {
        lastReq.url = url;

        var mod = modules[url];
        if (url.indexOf (".html") == url.length - 5) {
            // @FIXME: simplify this code to get the name of the resource
            // (without the extension)
            var resName = getResourceName (url);
            var id = resName.substr (0, resName.length - 5);
            mod.value = document.createElement (id);
        }
        else {
            mod.value = lastReq.module;
        }

        curCtx = getContextForUrl (url);

        var reqs = deferredReqs;
        deferredReqs = [];
        for (var i=0; i<reqs.length; i++) {
            reqs[i].ctx = curCtx;
            loadDeps (reqs[i], curCtx);
        }

        curCtx = null;

        completeLoad (mod);
    }

    function scriptOnLoad (evt) {
        if (this.tagName.toUpperCase () == "LINK" && this.import) {
            var template = this.import.querySelector ("template");
            // var clone = document.importNode (template.content, true);

            var script = template.content.children[2];

            if (script) {
                var node = document.createElement ("script");
                node.innerHTML = script.innerHTML;
                head.appendChild (node);
                head.removeChild (node);
            }
        }

        var url = this.getAttribute ("data-module-url");
        moduleOnLoad (url);

        head.removeChild (this);
    }

    function doLoad (mod) {
        if (mod.status == ModStatus.LOADING) // already loading
            return;
        else if (mod.status == ModStatus.READY) // already loaded
            completeLoad (mod);
        else {
            mod.status = ModStatus.LOADING;

            if (! inWorker) {
                if (mod.url.indexOf (".html") == mod.url.length - 5) {
                    var loader = document.createElement ("link");
                    loader.setAttribute ("rel", "import");
                    loader.setAttribute ("href", mod.url);
                    loader.setAttribute ("data-module-url", mod.url);
                    loader.addEventListener ("load", scriptOnLoad);
                    head.appendChild (loader);
                }
                else {
                    var script = document.createElement ("script");
                    script.type = "text/javascript";
                    script.async = true;
                    script.src = mod.url;
                    script.onload = scriptOnLoad;
                    script.setAttribute ("data-module-url", mod.url);
                    head.appendChild (script);
                }
            }
            else {
                importScripts (mod.url);
                moduleOnLoad (mod.url);
            }
        }
    }

    function depsReady (req) {
        reqCount--;
        if (reqCount == 0)
            curCtx = defCtx;

        if (req.ctx)
            localUrl = req.ctx.baseUrl;
        req.module = req.cb.apply (self, req.resources);
        localUrl = null;

        if (req.isDefine) {
            // @TODO: validate module definition output to be something
            // other than null or undefined.

            if (req.url) {
                var mod = modules[req.url];
                mod.value = req.module;
                completeLoad (mod);
            }
        }
    }

    function newRequest (deps, callback, errback, ctx, isDefine) {
        var req = {
            cb: callback,
            eb: errback,
            url: null,
            deps: deps,
            resources: [],
            resolved: 0,
            module: null,
            isDefine: isDefine
        };

        if (ctx)
            req.ctx = {
                baseUrl: ctx.baseUrl,
                rootUrl: ctx.rootUrl
            };

        return req;
    }

    function newModule (url) {
        var mod = {
            url: url,
            value: null,
            children: [],
            status: ModStatus.NONE
        };

        modules[url] = mod;

        return mod;
    }

    function loadDeps (req, ctx) {
        var deps = req.deps;

        for (var i=0; i<deps.length; i++) {
            var absUrl = resolveAbsUrl (deps[i], ctx);
            var basePath = getBaseUrl (absUrl);
            var resName = getResourceName (absUrl);
            var fullResUrl = basePath + resName;

            var mod = modules[fullResUrl];
            if (! mod)
                mod = newModule (fullResUrl);

            mod.children.push ({req: req, index: i});

            doLoad (mod);
        }
    }

    function load (deps, callback, errback, isDefine) {
        var req = newRequest (deps, callback, errback, curCtx, isDefine);
        lastReq = req;
        reqCount++;

        if (deps.length == 0)
            depsReady (req);
        else if (! curCtx) {
            if (reqCount == 1)
                loadDeps (req, defCtx);
            else
                deferredReqs.push (req);
        }
        else
            loadDeps (req, curCtx);

        curCtx = null;
    }

    // define
    var define = global["define"] = function (deps, callback, errback) {
        load (deps, callback, errback, true);
    };

    define.amd = function () {
        if (! inWorker) {
            var scripts = document.getElementsByTagName ("script");
            var script = scripts.item (scripts.length - 1);
            var mod = modules[script.src];

            return mod && mod.status == ModStatus.LOADING;
        }
        else {
            // @TODO: decide whether a script loading from worker was invoked
            // through importScripts or from require
        }

        return false;
    };

    // require
    var require = global["require"] = function (deps, callback, errback, extraArg) {
        if (deps.constructor === Object) {
            var config = deps;
            deps = callback;
            callback = errback;
            errback = extraArg;

            if (deps && callback)
                require (deps, callback, errback);
        }
        else {
            load (deps, callback, errback, false);
        }
    };

    Object.defineProperty (require, "localUrl", {
        "get": function () {
            return localUrl;
        }
    });
    Object.defineProperty (require, "baseUrl", {
        "get": function () {
            return defBaseUrl;
        }
    });

    require.config = function (obj) {
        defBaseUrl = obj.baseUrl;

        defCtx = curCtx = getContextForUrl (defBaseUrl);
    };

    require.amd = true;

    if (! inWorker) {
        // determine the local path and main script from the 'data-main' argument
        // of current script tag
        var scripts = document.getElementsByTagName ("script");
        var script = scripts.item (scripts.length - 1);

        // load main script, if specified
        var dataMain = script.getAttribute ("data-main");
        if (dataMain) {
            defBaseUrl = getBaseUrl (getAbsUrl (dataMain));
            var mainRes = getResourceName (dataMain);

            script = document.createElement ("script");
            script.setAttribute ("src", defBaseUrl + mainRes);

            head.appendChild (script);
        }
        else {
            defBaseUrl = getBaseUrl (getAbsUrl (script.src));
        }

        defCtx = curCtx = getContextForUrl (defBaseUrl);
    }
}) (this);
