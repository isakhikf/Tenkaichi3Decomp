# UDE-DB1 — candidata offline, base 0.1.39.6

Esta é uma candidata separada da C3. As duas assistências começam **desligadas** no pacote Windows.

1. Extraia o pacote em uma pasta nova. Mantenha a instalação e o lançador da C3 nas suas pastas.
2. Abra `Windows/Tenkaichi3Decomp/Jogar-UDE-DB1.cmd`. O arquivo `dados-locais.txt` aponta para os dados já extraídos da sua instalação.
3. Em treino offline, **F6** liga/desliga a DB1 e salva a escolha. O indicador mostra `DB1 ON`. **F7** pertence ao motor antigo; mantenha-o desligado nesta comparação.

Input físico tem prioridade: botões, direção, graça manual, ação iniciada e pedido pendente impedem intervenções. A DB1 fica fora de online, replay e lado CPU. Ela só acrescenta comandos à palavra de entrada; não altera janelas, vida, ki, posição ou resultados do jogo.

O código e o banco estão nos arquivos `UDE-DB1-fonte.zip` e `UDE-DB1-banco.zip`. O banco inclui o JSON v1.1 e o SQLite mesclado, preservando as 30 tabelas antigas. O jogo usa as tabelas C compiladas e não consulta SQLite durante a luta.

Z-Counter, sidestep e recovery são opcionais e começam desligados. Skills, transformação, clashes, Emergency Blaster Wave e vanish behind não são receitas automáticas. Dragon Dash Escape não foi automatizado porque exigiria CHARGE além dos bits autorizados.

Consulte `RELATORIO.md` e `BUILD.json` para o resultado e a identificação do build. O roteiro físico está em `TESTE-MANUAL.md`; `tentativas-manuais.csv` tem dez tentativas por técnica. **A candidata não é homologada.** Os logs históricos C3/C5 não permitem calcular a taxa de sucesso que a DB1 teria obtido.

Para gravar os novos logs, use `ude_db_log=1` em `bt3_settings.txt` antes de abrir o jogo. O arquivo gerado é `ude_db_log.jsonl`; feche o jogo normalmente para descarregar o buffer. Para comparar desempenho gráfico, deixe esse log desligado.
