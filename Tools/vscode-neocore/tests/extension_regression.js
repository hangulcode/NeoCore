const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const vm = require("vm");
const { fileURLToPath, pathToFileURL } = require("url");

const root = path.resolve(__dirname, "..");
const manifest = JSON.parse(fs.readFileSync(path.join(root, "package.json"), "utf8"));
assert.deepStrictEqual(manifest.contributes.languages[0].extensions, [".nc"]);
assert.strictEqual(manifest.contributes.debuggers[0].type, "neocore");
for (const grammar of manifest.contributes.grammars) JSON.parse(fs.readFileSync(path.join(root, grammar.path), "utf8"));
for (const snippet of manifest.contributes.snippets) JSON.parse(fs.readFileSync(path.join(root, snippet.path), "utf8"));

async function run(executable) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "neocore-extension-"));
  const sourcePath = path.join(directory, "sample.nc");
  const source = "import math;\nfloat value=1.0;\nprint(math.sqrt(value));\n";
  fs.writeFileSync(sourcePath, source);
  const uri = (text) => ({ fsPath: fileURLToPath(text), toString: () => text });
  const document = { languageId: "neocore", uri: uri(pathToFileURL(sourcePath).toString()), version: 1, getText: () => source };
  const diagnostics = new Map();
  const providers = {};
  const commands = {};
  const noop = () => ({ dispose() {} });
  const vscode = {
    workspace: {
      isTrusted: true,
      workspaceFolders: [{ uri: uri(pathToFileURL(directory).toString()) }],
      textDocuments: [document],
      getConfiguration: () => ({ get: () => executable }),
      onDidOpenTextDocument: noop, onDidChangeTextDocument: noop, onDidCloseTextDocument: noop
    },
    window: { activeTextEditor: { document }, showErrorMessage: (message) => { throw new Error(message); } },
    commands: { registerCommand: (name, handler) => { commands[name] = handler; return noop(); }, executeCommand: noop },
    languages: {
      createDiagnosticCollection: () => ({ set: (key, values) => diagnostics.set(key.toString(), values), clear: () => diagnostics.clear(), dispose() {} }),
      registerCompletionItemProvider: (_, provider) => { providers.completion = provider; return noop(); },
      registerSignatureHelpProvider: (_, provider) => { providers.signature = provider; return noop(); },
      registerHoverProvider: (_, provider) => { providers.hover = provider; return noop(); }
    },
    debug: {
      registerDebugConfigurationProvider: (_, provider) => { providers.debug = provider; return noop(); },
      registerDebugAdapterDescriptorFactory: (_, provider) => { providers.adapter = provider; return noop(); },
      onDidStartDebugSession: noop, onDidReceiveDebugSessionCustomEvent: noop
    },
    Uri: { parse: uri, file: (file) => uri(pathToFileURL(file).toString()) },
    CompletionItemKind: { Text: 0 }, DiagnosticSeverity: { Error: 0 },
    CompletionItem: class { constructor(label, kind) { this.label = label; this.kind = kind; } },
    Range: class { constructor(...args) { this.args = args; } },
    Diagnostic: class { constructor(range, message, severity) { Object.assign(this, { range, message, severity }); } },
    SignatureHelp: class {},
    SignatureInformation: class { constructor(label) { this.label = label; } },
    ParameterInformation: class { constructor(label) { this.label = label; } },
    MarkdownString: class { constructor(value) { this.value = value; } },
    Hover: class { constructor(contents) { this.contents = contents; } },
    DebugAdapterExecutable: class { constructor(command, args, options) { Object.assign(this, { command, args, options }); } }
  };
  const moduleObject = { exports: {} };
  const children = [];
  const childProcess = { ...require("child_process"), spawn(...args) {
    const child = require("child_process").spawn(...args);
    children.push(child);
    return child;
  } };
  const sandbox = {
    module: moduleObject,
    require: (name) => name === "vscode" ? vscode : name === "child_process" ? childProcess : require(name),
    Buffer, process, setTimeout, clearTimeout, console
  };
  vm.runInNewContext(fs.readFileSync(path.join(root, "src/extension.js"), "utf8"), sandbox);
  try {
    moduleObject.exports.activate({ extensionPath: root, subscriptions: [] });
    const items = await providers.completion.provideCompletionItems(document, { line: 2, character: 13 });
    assert(items.some((item) => item.label === "sqrt"), JSON.stringify(items));
    assert.strictEqual(items.find((item) => item.label === "sqrt").kind, 2);
    const help = await providers.signature.provideSignatureHelp(document, { line: 2, character: 16 });
    assert(help && help.signatures[0].label.includes("float math.sqrt"));
    const hover = await providers.hover.provideHover(document, { line: 2, character: 13 });
    assert(hover && hover.contents.value.includes("float math.sqrt"));
    const config = providers.debug.resolveDebugConfiguration(vscode.workspace.workspaceFolders[0], {
      type: "neocore", request: "launch", program: sourcePath, adapterPath: executable, cwd: directory
    });
    assert.strictEqual(config.program, sourcePath);
    const adapter = providers.adapter.createDebugAdapterDescriptor({ workspaceFolder: vscode.workspace.workspaceFolders[0], configuration: config });
    assert.strictEqual(adapter.command, executable);
    assert.strictEqual(adapter.args[0], "--dap");
    assert(commands["neoCore.createLaunchConfig"] && commands["neoCore.openSettings"]);
    assert.strictEqual(diagnostics.get(document.uri.toString()).length, 0);
  } finally {
    moduleObject.exports.deactivate();
    await Promise.all(children.map((child) => child.exitCode !== null ? Promise.resolve() : new Promise((resolve) => {
      const timer = setTimeout(() => child.kill(), 2000);
      child.once("exit", () => { clearTimeout(timer); resolve(); });
    })));
    fs.rmSync(directory, { recursive: true, force: true });
  }
}

if (process.argv[2]) {
  run(path.resolve(process.argv[2])).then(() => console.log("NeoCore extension regression passed"), (error) => { console.error(error); process.exitCode = 1; });
} else {
  console.log("NeoCore extension manifest and resources passed; supply neocore to test the language/debug adapters");
}
