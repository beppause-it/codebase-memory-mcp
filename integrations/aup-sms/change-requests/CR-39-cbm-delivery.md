# CR39 — Schemi effettivi del profilo MCP HTTP

29 ottobre 2026. Implementazione disponibile e qualificata Windows.
Collaudo consumer ASMS/Agent OS e end-to-end Linux non dichiarati.

## Correzione

Gli schemi HTTP derivano ora dalla configurazione effettiva della richiesta:
limit, max_rows, semantic_limit, depth e max_output_tokens intersecano i vincoli
nativi con i ceiling HTTP. Lo stesso schema è usato da tools/list, validazione
e applicazione dei default omessi, anche nel worker. Il reload configurato è
quello già esistente: nessuna cache aggiuntiva o nuovo registro di limiti.

Sono mantenuti i ceiling e i rifiuti precedenti; zero per max_rows e
semantic_limit rimane vietato nel profilo HTTP, mentre lo stdio resta invariato.
Il default semantic_limit viene ora limitato anche quando omesso, evitando che
il default nativo superi una configurazione HTTP più restrittiva. Il budget
predefinito max_output_tokens resta quello del profilo HTTP precedente.

TOOL_ARGUMENTS_REJECTED e il messaggio legacy rimangono invariati. details
aggiunge reason, tool, parameter, path, expected_type e minimum/maximum se
applicabili. Nessuna query, valore respinto o body viene copiato nei dettagli;
nomi di proprietà e tool sconosciuti sono redatti. Il diagnostico distribuito
CR37 conserva questi dettagli, con lo stack nativo non disponibile dichiarato.

## Informazioni per l'LLM e compatibilità

Lo schema pubblica source_id come stringa obbligatoria, formato e lunghezza;
la descrizione richiede l'identificatore reale fornito dall'integrazione.
I percorsi sono esplicitamente relativi alla sorgente. Le descrizioni HTTP di
query_graph e get_architecture non promettono default nativi incompatibili,
cursor o polling job non disponibili nel profilo. Restano soltanto proprietà
e aspetti consentiti.

Il contratto resta asms-cbm-http/v1: la restrizione dello schema corregge la
descrizione di vincoli già applicati, senza ampliare policy o modificare
richieste esplicite prima ammesse. La nuova baseline componente è
0.12.0-asms.cr39.1. I consumer devono rileggere tools/list dopo l'aggiornamento
o in una nuova sessione; nessuna ricreazione di agenti/preset è richiesta.

Una chiamata conforme può comunque fallire per binding inattivo/scaduto,
query semanticamente invalida, path inesistente, budget finale o policy di sola
lettura. Nessuna attivazione di sorgenti reali o correzione consumer implicita.
Non è ancora dichiarato un collaudo end-to-end ASMS/Agent OS.

## Prove

Il nuovo test contro CR38 riproduce il difetto: schema semantic_limit massimo
500 con ceiling HTTP 100. Log `build/cr39-baseline.log`, FAIL atteso.

Nuovi test reali HTTP coprono tutti i sei tool, valori min/default/max e fuori
range, omissione, configurazioni 100/8, 7/2, 1/1 e 300/20 ricaricate, dettagli
diagnostici redatti ed esecuzione con artefatto attivo. La suite esistente
conserva stdio, SDK standard, auth, isolamento sorgenti e sola lettura.

La CR38 è preservata, inclusi sorgenti e EXE precedente. Nessun commit/push,
modifica .env, upgrade consumer o deployment eseguito.

## Artefatto e prove disponibili

EXE con UI: `build/cr39-native/codebase-memory-mcp.exe`,
`0.12.0-asms.cr39.1`, 335390208 byte, SHA256
`9936986cfb63c1fe8944607492f0c16165359644003a4d202a7e51380e9fd866`.
Build pulita ufficiale: `build/cr39-build.log`. Le ultime sole descrizioni C
sono incluse nel relink dello stesso target con asset UI invariati:
`build/cr39-relink.log`.

Schema prima/dopo ottenuto via HTTP dai binari reali, senza binding o segreti:
`build/cr39-schema-before.json` e `build/cr39-schema-after.json`.
Configurazione predefinita, intervalli/default effettivamente pubblicati:

| Parametro | Prima | Dopo |
| --- | --- | --- |
| query_graph.max_rows | min 0, nessun max, default 200 | 1–100, default 100 |
| search_graph.semantic_limit | 0–500, default 50 | 1–100, default 50 |
| trace_path.depth | 1–15, default 3 | 1–8, default 3 |
| query_graph.max_output_tokens | 128–1000000, default non pubblicato | 128–262144, default 262144 |

Tre test mirati PASS, `build/cr39-focused-tests.log`. Suite completa sul binario
finale: 44 test PASS, 465.847 secondi, exit 0,
`build/cr39-http-tests.log`. Include SDK standard, stdio invariato, limiti
ricaricati, default eseguiti, diagnostici, auth e isolamento sorgenti.
La UI finale serve HTML e asset ed esce
correttamente: PASS, `build/cr39-ui-smoke.log`. Controllo sintattico Linux
finale con GCC -Wall -Wextra -Werror: PASS, `build/cr39-linux-syntax.log`;
questo non costituisce collaudo end-to-end Linux.
