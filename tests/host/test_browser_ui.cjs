const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const web = path.resolve(__dirname, '../../spiffs_image/www');

function browser(file, inputData) {
    const elements = new Map();
    const requests = [];
    const page = {querySelector: () => ({})};
    function element(selector) {
        if (!elements.has(selector)) elements.set(selector, {
            handlers: {}, files: [], children: [], style: {},
            addEventListener() {}, classList: {add() {}, remove() {}},
            on(event, ...args) { this.handlers[event] = args.at(-1); return this; },
            val(value) { if (arguments.length) this.value = value; return this.value; },
            get() { return this; }, hide() {}, show() {}, text() {}, attr() {},
            empty() { return this; }, append() { return this; },
            fadeOut(duration, callback) { callback?.(); },
        });
        return elements.get(selector);
    }
    const $ = element;
    $.getq = (queue, url, data, callback) => {
        requests.push({url, callback: typeof data === 'function' ? data : callback});
    };
    $.postq = (queue, url, data) => {
        requests.push({url, data});
        if (typeof data === 'function' && url.endsWith('/srom/getSize')) data('1000000');
    };
    $.ajaxq = (queue, request) => requests.push(request);
    const context = {
        $, console: {log() {}}, window: {favorites: {selected: 0, data: []}},
        document: {getElementById: id => element('#' + id), body: element('body')},
        ons: {getScriptPage: () => page}, Sortable: {create() {}},
        FileReader: class {
            readAsText() { this.onloadend({target: {result: JSON.stringify(inputData)}}); }
            readAsArrayBuffer() { this.onloadend({target: {result: inputData}}); }
        },
        DataView, Uint8Array, Uint16Array, Uint32Array, Int16Array,
    };
    vm.createContext(context);
    const html = fs.readFileSync(path.join(web, file), 'utf8');
    for (const match of html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g))
        if (match[1].trim()) vm.runInContext(match[1], context, {filename: file});
    page.onInit?.call(page);
    return {element, requests};
}

for (const index of [1, 4]) {
    const preset = {params: [{id: 'tune', current: 99}]};
    const data = {fav: {plug_0: 'SynthA', plug_1: 'SynthB', pre_0: index, pre_1: index},
                  preset_0: preset, preset_1: preset};
    const ui = browser('main.html', data);
    ui.element('#fav-import-file').files = [{}];
    ui.element('#fav-import-file').handlers.change();
    for (const request of [...ui.requests]) {
        if (request.url.startsWith('/api/v1/getPresetData/'))
            request.callback({presets: {patches: [{params: [{id: 'tune', current: 0}]}]}});
    }
    const writes = ui.requests.filter(r => r.url.startsWith('/api/v1/setPresetData/'));
    assert.equal(writes.length, 2);
    for (const write of writes) {
        const patches = JSON.parse(write.data).patches;
        assert.equal(patches.length, index + 1);
        assert.equal(patches[index].params[0].current, 99);
    }
}

for (const [magic, length, accepted] of [
    [0xdeadface, 12, true], [0x0000face, 12, false],
    [0xdead0000, 12, false], [0xdeadface, 4, false], [0, 0, false],
]) {
    const bytes = new ArrayBuffer(length);
    if (length >= 4) new DataView(bytes).setUint32(0, magic, true);
    const ui = browser('sample-rom.html', bytes);
    ui.element('#upload-raw-from-file').files = [{size: length}];
    ui.element('#upload-raw-from-file').handlers.change();
    const erased = ui.requests.some(r => r.url === '/api/v1/srom/erase');
    assert.equal(erased, accepted, `magic=${magic.toString(16)}, length=${length}`);
}
console.log('7 browser UI regressions passed.');
