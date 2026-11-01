const fs = require("fs");
const fsp = require("fs/promises");
const path = require("path");
const { spawn } = require("child_process");
const vscode = require("vscode");

let diagnosticCollection;
let languageClient;

function resolveVariables(value, folder) {
  if (!value || typeof value !== "string") {
    return value;
  }

  const workspaceFolder = folder ? folder.uri.fsPath : "";
  const editor = vscode.window.activeTextEditor;
  const file = editor ? editor.document.uri.fsPath : "";
  return value
    .replace(/\$\{workspaceFolder\}/g, workspaceFolder)
    .replace(/\$\{file\}/g, file);
}

function findAdapterPath(context, folder, explicitPath) {
  const configured = explicitPath || vscode.workspace.getConfiguration("neoCore", folder?.uri).get("debugAdapterPath");
  if (configured) return resolveVariables(configured, folder);
  const roots = [folder?.uri.fsPath, path.resolve(context.extensionPath, "..", "..")].filter(Boolean);
  const name = process.platform === "win32" ? "neocore.exe" : "neocore";
  const candidates = roots.flatMap((root) => [
    path.join(root, "Build", "Windows", "bin", "x64", "Release", name),
    path.join(root, "Build", "Windows", "bin", "x64", "Debug", name),
    path.join(root, "Build", "neocore", "bin", "Release", name),
    path.join(root, "Build", "neocore", "bin", "Debug", name),
    path.join(root, "Build", "neocore", "bin", name),
    path.join(root, "build", "bin", name)
  ]);
  return candidates.find((candidate) => fs.existsSync(candidate));
}

async function pathExists(filePath) {
  try {
    await fsp.access(filePath);
    return true;
  } catch (_) {
    return false;
  }
}

function getLaunchTemplate() {
  return {
    type: "neocore",
    request: "launch",
    name: "Debug NeoCore",
    program: "${file}",
    cwd: "${workspaceFolder}",
    libPath: "${workspaceFolder}/Lib"
  };
}

async function createLaunchConfig() {
  const folder = vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0];
  if (!folder) {
    vscode.window.showErrorMessage("Open a workspace folder before creating a NeoCore launch config.");
    return;
  }

  const vscodeDir = path.join(folder.uri.fsPath, ".vscode");
  const launchPath = path.join(vscodeDir, "launch.json");
  await fsp.mkdir(vscodeDir, { recursive: true });

  const template = getLaunchTemplate();
  let launch = { version: "0.2.0", configurations: [template] };

  if (await pathExists(launchPath)) {
    let parsed;
    try {
      const current = await fsp.readFile(launchPath, "utf8");
      parsed = JSON.parse(current);
    } catch (error) {
      vscode.window.showErrorMessage(`Could not update launch.json: ${error.message}`);
      return;
    }

    if (!Array.isArray(parsed.configurations)) {
      parsed.configurations = [];
    }

    const alreadyExists = parsed.configurations.some((config) => config && config.name === template.name);
    if (!alreadyExists) {
      parsed.configurations.push(template);
    }
    launch = parsed;
  }

  await fsp.writeFile(launchPath, `${JSON.stringify(launch, null, 2)}\n`, "utf8");
  await vscode.window.showTextDocument(vscode.Uri.file(launchPath));
  vscode.window.showInformationMessage("NeoCore launch config was created.");
}

class NeoCoreDebugConfigurationProvider {
  constructor(context) {
    this.context = context;
  }

  resolveDebugConfiguration(folder, config) {
    if (!config.type && !config.request && !config.name) {
      const editor = vscode.window.activeTextEditor;
      config.type = "neocore";
      config.request = "launch";
      config.name = "Debug NeoCore";
      config.program = editor && editor.document.languageId === "neocore"
        ? editor.document.uri.fsPath
        : "${file}";
      config.cwd = "${workspaceFolder}";
    }

    config.program = resolveVariables(config.program, folder);
    config.cwd = resolveVariables(config.cwd || "${workspaceFolder}", folder);
    config.libPath = resolveVariables(config.libPath, folder) || path.join(config.cwd || path.dirname(config.program), "Lib");
    config.adapterPath = findAdapterPath(this.context, folder, config.adapterPath);

    if (config.program && path.extname(config.program) !== ".nc") {
      vscode.window.showErrorMessage("NeoCore debugging requires a .nc file.");
      return undefined;
    }

    if (!config.program) {
      vscode.window.showErrorMessage("NeoCore debug launch needs a program path.");
      return undefined;
    }

    if (!fs.existsSync(config.program)) {
      vscode.window.showErrorMessage(`NeoCore file not found: ${config.program}`);
      return undefined;
    }

    if (!config.adapterPath || !fs.existsSync(config.adapterPath)) {
      vscode.window.showErrorMessage(
        "NeoCore debug adapter was not found. Build the NeoCore console (root NeoCore.sln) or set neoCore.debugAdapterPath."
      );
      return undefined;
    }

    return config;
  }
}

class NeoCoreDebugAdapterDescriptorFactory {
  constructor(context) {
    this.context = context;
  }

  createDebugAdapterDescriptor(session) {
    const folder = session.workspaceFolder;
    const adapterPath = findAdapterPath(this.context, folder, session.configuration.adapterPath);
    const cwd = resolveVariables(session.configuration.cwd || "${workspaceFolder}", folder) || path.dirname(adapterPath);
    return new vscode.DebugAdapterExecutable(adapterPath, ["--dap"], {
      cwd
    });
  }
}

class NeoCoreLanguageClient {
  constructor(executablePath, cwd, args) {
    this.args = args;
    this.executablePath = executablePath;
    this.cwd = cwd;
    this.process = undefined;
    this.buffer = Buffer.alloc(0);
    this.nextRequestId = 1;
    this.pendingRequests = new Map();
  }

  start() {
    if (this.process) {
      return true;
    }

    try {
      this.process = spawn(this.executablePath, this.args, {
        cwd: this.cwd,
        stdio: ["pipe", "pipe", "ignore"],
        windowsHide: true
      });
    } catch (_) {
      this.process = undefined;
      return false;
    }

    this.process.stdout.on("data", (data) => this.onData(data));
    this.process.on("exit", () => this.onExit());
    this.process.on("error", () => this.onExit());
    this.request("initialize", { processId: process.pid, rootUri: null, capabilities: {} })
      .then(() => this.notify("initialized", {}));
    return true;
  }

  onExit() {
    this.process = undefined;
    for (const resolve of this.pendingRequests.values()) {
      resolve(undefined);
    }
    this.pendingRequests.clear();
  }

  onData(data) {
    this.buffer = Buffer.concat([this.buffer, data]);
    while (true) {
      const headerEnd = this.buffer.indexOf("\r\n\r\n");
      if (headerEnd < 0) {
        return;
      }

      const header = this.buffer.subarray(0, headerEnd).toString("ascii");
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) {
        this.buffer = Buffer.alloc(0);
        return;
      }

      const contentLength = Number(match[1]);
      const messageEnd = headerEnd + 4 + contentLength;
      if (this.buffer.length < messageEnd) {
        return;
      }

      const text = this.buffer.subarray(headerEnd + 4, messageEnd).toString("utf8");
      this.buffer = this.buffer.subarray(messageEnd);
      try {
        const message = JSON.parse(text);
        if (message.method === "textDocument/publishDiagnostics") {
          setLspDiagnostics(message.params);
          continue;
        }
        if (message && Object.prototype.hasOwnProperty.call(message, "id")) {
          const resolve = this.pendingRequests.get(message.id);
          if (resolve) {
            this.pendingRequests.delete(message.id);
            resolve(message.result);
          }
        }
      } catch (_) {
        // 잘못된 서버 출력이 편집기 동작을 막지 않게 한다.
      }
    }
  }

  send(message) {
    if (!this.process || !this.process.stdin.writable) {
      return false;
    }

    const body = Buffer.from(JSON.stringify(message), "utf8");
    this.process.stdin.write(`Content-Length: ${body.length}\r\n\r\n`);
    this.process.stdin.write(body);
    return true;
  }

  notify(method, params) {
    return this.send({ jsonrpc: "2.0", method, params });
  }

  request(method, params) {
    if (!this.process) {
      return Promise.resolve(undefined);
    }

    const id = this.nextRequestId++;
    return new Promise((resolve) => {
      const timeout = setTimeout(() => {
        if (this.pendingRequests.delete(id)) {
          resolve(undefined);
        }
      }, 5000);
      this.pendingRequests.set(id, (result) => {
        clearTimeout(timeout);
        resolve(result);
      });
      if (!this.send({ jsonrpc: "2.0", id, method, params })) {
        this.pendingRequests.delete(id);
        clearTimeout(timeout);
        resolve(undefined);
      }
    });
  }

  openDocument(document) {
    this.notify("textDocument/didOpen", {
      textDocument: {
        uri: document.uri.toString(),
        languageId: document.languageId,
        version: document.version,
        text: document.getText()
      }
    });
  }

  changeDocument(document) {
    this.notify("textDocument/didChange", {
      textDocument: { uri: document.uri.toString(), version: document.version },
      contentChanges: [{ text: document.getText() }]
    });
  }

  closeDocument(document) {
    this.notify("textDocument/didClose", {
      textDocument: { uri: document.uri.toString() }
    });
  }

  async completion(document, position) {
    const result = await this.request("textDocument/completion", {
      textDocument: { uri: document.uri.toString() },
      position: { line: position.line, character: position.character }
    });
    return result && Array.isArray(result.items) ? result.items : [];
  }

  signatureHelp(document, position) {
    return this.request("textDocument/signatureHelp", {
      textDocument: { uri: document.uri.toString() },
      position: { line: position.line, character: position.character }
    });
  }

  dispose() {
    if (!this.process) {
      return;
    }
    this.request("shutdown", {}).finally(() => {
      this.notify("exit", {});
      if (this.process) {
        this.process.kill();
      }
    });
  }
}

function isNeoCoreDocument(document) {
  return document && document.languageId === "neocore";
}

function findLanguageServerPath(context, folder) {
  const configured = vscode.workspace.getConfiguration("neoCore", folder?.uri).get("languageServerPath");
  return configured ? resolveVariables(configured, folder) : findAdapterPath(context, folder);
}

function startLanguageClient(context) {
  if (languageClient && languageClient.process) {
    return languageClient;
  }

  const folder = vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0];
  const executablePath = findLanguageServerPath(context, folder);
  if (!executablePath || !fs.existsSync(executablePath)) {
    return undefined;
  }

  const cwd = folder ? folder.uri.fsPath : path.dirname(executablePath);
  const config = vscode.workspace.getConfiguration("neoCore", folder?.uri);
  const args = ["--lsp", "--max-errors", String(config.get("maxDiagnostics", 100))];
  for (const [setting, option] of [["hostSchema", "--host"], ["hostImplementation", "--host-impl"], ["resourceRoot", "--resource-root"]]) {
    const value = config.get(setting, "");
    if (value) args.push(option, resolveVariables(value, folder));
  }
  languageClient = new NeoCoreLanguageClient(executablePath, cwd, args);
  if (!languageClient.start()) {
    languageClient = undefined;
    return undefined;
  }

  for (const document of vscode.workspace.textDocuments) {
    if (isNeoCoreDocument(document)) {
      languageClient.openDocument(document);
    }
  }
  return languageClient;
}

function toCompletionItem(item) {
  const completion = new vscode.CompletionItem(item.label, item.kind ? item.kind - 1 : vscode.CompletionItemKind.Text);
  completion.detail = item.detail;
  completion.documentation = item.documentation;
  completion.insertText = item.insertText || item.label;
  return completion;
}

function toSignatureHelp(result) {
  if (!result || !Array.isArray(result.signatures) || result.signatures.length === 0) {
    return undefined;
  }

  const help = new vscode.SignatureHelp();
  help.activeSignature = result.activeSignature || 0;
  help.activeParameter = result.activeParameter || 0;
  help.signatures = result.signatures.map((signature) => {
    const information = new vscode.SignatureInformation(signature.label, signature.documentation);
    information.parameters = (signature.parameters || []).map(
      (parameter) => new vscode.ParameterInformation(parameter.label, parameter.documentation)
    );
    return information;
  });
  return help;
}

function setLspDiagnostics(params) {
  if (!diagnosticCollection || !params?.uri) return;
  const diagnostics = (params.diagnostics || []).map((item) => {
    const start = item.range.start;
    const end = item.range.end;
    const value = new vscode.Diagnostic(new vscode.Range(start.line, start.character, end.line, end.character), item.message, vscode.DiagnosticSeverity.Error);
    value.source = "NeoCore";
    return value;
  });
  diagnosticCollection.set(vscode.Uri.parse(params.uri), diagnostics);
}

function setCompileDiagnostic(body) {
  if (!diagnosticCollection || !body || !body.source || !body.source.path) {
    return;
  }

  const line = Math.max(0, (body.line || 1) - 1);
  const column = Math.max(0, (body.column || 1) - 1);
  const message = body.message || body.raw || "NeoCore compile failed.";
  const uri = vscode.Uri.file(body.source.path);
  const range = new vscode.Range(line, column, line, column + 1);
  const diagnostic = new vscode.Diagnostic(range, message, vscode.DiagnosticSeverity.Error);
  diagnostic.source = "NeoCore";

  diagnosticCollection.set(uri, [diagnostic]);
}

function activate(context) {
  if (!vscode.workspace.isTrusted) return;
  diagnosticCollection = vscode.languages.createDiagnosticCollection("neocore");
  context.subscriptions.push(diagnosticCollection);

  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider(
      "neocore",
      new NeoCoreDebugConfigurationProvider(context)
    )
  );
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory(
      "neocore",
      new NeoCoreDebugAdapterDescriptorFactory(context)
    )
  );
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(
      "neocore",
      {
        async provideCompletionItems(document, position) {
          const client = startLanguageClient(context);
          if (!client) {
            return [];
          }
          return (await client.completion(document, position)).map(toCompletionItem);
        }
      },
      "."
    )
  );
  context.subscriptions.push(
    vscode.languages.registerSignatureHelpProvider(
      "neocore",
      {
        async provideSignatureHelp(document, position) {
          const client = startLanguageClient(context);
          if (!client) {
            return undefined;
          }
          return toSignatureHelp(await client.signatureHelp(document, position));
        }
      },
      "(",
      ","
    )
  );
  context.subscriptions.push(
    vscode.languages.registerHoverProvider("neocore", {
      async provideHover(document, position) {
        const client = startLanguageClient(context);
        if (!client) return undefined;
        const result = await client.request("textDocument/hover", {
          textDocument: { uri: document.uri.toString() }, position: { line: position.line, character: position.character }
        });
        return result?.contents?.value ? new vscode.Hover(new vscode.MarkdownString(result.contents.value)) : undefined;
      }
    })
  );
  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((document) => {
      if (isNeoCoreDocument(document) && languageClient) {
        languageClient.openDocument(document);
      }
    })
  );
  context.subscriptions.push(
    vscode.workspace.onDidChangeTextDocument((event) => {
      if (isNeoCoreDocument(event.document) && languageClient) {
        languageClient.changeDocument(event.document);
      }
    })
  );
  context.subscriptions.push(
    vscode.workspace.onDidCloseTextDocument((document) => {
      if (isNeoCoreDocument(document) && languageClient) {
        languageClient.closeDocument(document);
      }
    })
  );
  context.subscriptions.push(
    vscode.commands.registerCommand("neoCore.createLaunchConfig", createLaunchConfig)
  );
  context.subscriptions.push(
    vscode.commands.registerCommand("neoCore.openSettings", () => {
      vscode.commands.executeCommand("workbench.action.openSettings", "neoCore.debugAdapterPath");
    })
  );
  context.subscriptions.push(
    vscode.debug.onDidStartDebugSession((session) => {
      if (session.type === "neocore") {
        diagnosticCollection.clear();
      }
    })
  );
  context.subscriptions.push(
    vscode.debug.onDidReceiveDebugSessionCustomEvent((event) => {
      if (event.session.type !== "neocore") {
        return;
      }
      if (event.event === "neoCoreCompileError") {
        setCompileDiagnostic(event.body);
      }
    })
  );
  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (!event.affectsConfiguration("neoCore")) return;
      if (languageClient) languageClient.dispose();
      languageClient = undefined;
      diagnosticCollection.clear();
      startLanguageClient(context);
    })
  );
  startLanguageClient(context);
}

function deactivate() {
  if (languageClient) {
    languageClient.dispose();
    languageClient = undefined;
  }
  if (diagnosticCollection) {
    diagnosticCollection.dispose();
  }
}

module.exports = {
  activate,
  deactivate
};

