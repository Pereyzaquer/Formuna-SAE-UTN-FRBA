// Genera Code/ECU/ARQUITECTURA.pdf a partir de Code/ECU/ARQUITECTURA.md,
// con los diagramas Mermaid dibujados.
//
// Uso, desde Code/tools:
//   npm install          (una sola vez, baja "marked")
//   node arquitectura_pdf.js
//
// Necesita Google Chrome instalado: lo usa en modo headless para dibujar
// los diagramas e imprimir a PDF. Si Chrome esta en otra ruta, cambiar
// CHROME aca abajo.
//
// El PDF es una copia del .md para mandar o imprimir. La fuente de
// verdad sigue siendo el .md: cada vez que se edite, regenerar el PDF.

const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const CHROME = "C:/Program Files/Google/Chrome/Application/chrome.exe";
const ECU = path.resolve(__dirname, "..", "ECU");
const MD = path.join(ECU, "ARQUITECTURA.md");
const PDF = path.join(ECU, "ARQUITECTURA.pdf");
const TMP = path.join(ECU, "_arquitectura_tmp.html");

(async () => {
  const md = fs.readFileSync(MD, "utf8");

  // Se apartan los bloques mermaid antes de convertir, y se reponen
  // despues: asi marked no les toca el contenido.
  const diagramas = [];
  const sinDiagramas = md.replace(/```mermaid\n([\s\S]*?)```/g, (_, cuerpo) => {
    diagramas.push(cuerpo);
    return `<!--DIAGRAMA${diagramas.length - 1}-->`;
  });

  const { marked } = await import("marked");
  let cuerpo = await marked.parse(sinDiagramas);
  cuerpo = cuerpo.replace(/<!--DIAGRAMA(\d+)-->/g,
    (_, i) => `<div class="diagrama"><pre class="mermaid">${diagramas[i]}</pre></div>`);

  const html = `<!doctype html><html lang="es"><head><meta charset="utf-8">
<title>Arquitectura del firmware</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/mermaid/10.9.1/mermaid.min.js"></script>
<style>
  @page { size: A4; margin: 18mm 16mm; }
  body { font: 10.5pt/1.55 "Segoe UI", system-ui, sans-serif; color: #1b1f26; margin: 0; }
  h1 { font-size: 22pt; margin: 0 0 4pt; letter-spacing: -0.01em; }
  h2 { font-size: 14pt; margin: 22pt 0 8pt; padding-bottom: 4pt;
       border-bottom: 1.5px solid #1b1f26; break-after: avoid; }
  h3 { font-size: 11.5pt; margin: 14pt 0 6pt; break-after: avoid; }
  p, li { max-width: 62em; }
  code { font-family: Consolas, "Courier New", monospace; font-size: 9.5pt;
         background: #f0f2f5; padding: 1px 4px; border-radius: 3px; }
  a { color: #1b1f26; text-decoration: none; border-bottom: 1px solid #b9c0cc; }
  hr { border: 0; border-top: 1px solid #d8dde5; margin: 20pt 0; }
  table { border-collapse: collapse; font-size: 9.5pt; margin: 8pt 0; }
  th, td { border: 1px solid #d8dde5; padding: 4pt 8pt; text-align: left; }
  th { background: #f0f2f5; }
  .diagrama { break-inside: avoid; margin: 12pt 0; text-align: center; }
  .diagrama svg { max-width: 100%; height: auto; }
  ul { padding-left: 18pt; }
</style></head><body>
${cuerpo}
<script>
  mermaid.initialize({ startOnLoad: false, theme: "neutral",
                       flowchart: { htmlLabels: false } });
  mermaid.run({ querySelector: ".mermaid" });
</script></body></html>`;

  fs.writeFileSync(TMP, html, "utf8");

  // virtual-time-budget le da tiempo a mermaid a dibujar antes de imprimir.
  execFileSync(CHROME, [
    "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
    "--virtual-time-budget=20000",
    `--print-to-pdf=${PDF}`,
    "file:///" + TMP.replace(/\\/g, "/"),
  ], { stdio: "ignore" });

  fs.unlinkSync(TMP);

  const kb = Math.round(fs.statSync(PDF).size / 1024);
  console.log(`PDF generado: ${PDF} (${kb} kB, ${diagramas.length} diagramas)`);
})();
