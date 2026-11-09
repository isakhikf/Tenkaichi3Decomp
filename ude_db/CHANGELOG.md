# Changelog — UDE-DB1

## 2026-11-09 — fases 2–4 na branch nova

Verificações finais de posse manual: habilitar DB1 no meio de uma ação, pausar e alternar o toggle não apagam a posse manual nem o intervalo de pressão. Lacunas no registro suspendem novas pressões e exigem retorno a estado neutro.

| Arquivo | Motivo e evidência | Reversão |
|---|---|---|
| bt3_defesas_db.json | Acrescenta contrato escalar da candidata, defaults off e prioridades. Corrige a referência BtlDecide_Main de uma chamada em L2127 para a definição L2165, sem mudar os valores extraídos. | Usar o banco original preservado da fase 1; o gerador deve ser executado novamente. |
| tools/defense_db/build.py | Importação aditiva, tabelas static const, hash canônico e check de drift. Dados/evidências do AUDIT e contrato do pedido. | Remover os novos artefatos desta cópia; nenhum SQLite original foi modificado. |
| port/src/ude_db_tab.h | Gerado, Do not edit; 161×163 registros, 21 técnicas e janelas nativas. | Regenerar do JSON; não editar manualmente. |
| port/src/ude_db.c/.h | Política escalar independente; saída física OR contribuição; pisos, intervalos, bordas frescas e razões. Corpos nativos de input/timers/colisão citados na auditoria. | Desligar ude_db_enabled; o núcleo antigo permanece disponível. |
| port/src/ude_db_game.c | Adaptador exclusivo DB1, snapshot, log opcional com estado/configuração; recusa online/replay/CPU. | F6 desliga; usar ude_enabled=0 para deixar as duas assistências desligadas. |
| src/battle/btl_input.c | Troca a chamada para o dispatcher DB1 no ponto já integrado de BuildRecord. Não ativa injectOn nem remove bits. | Voltar à chamada PortUde_Filter nesta branch. |
| src/battle/btl_char_hit.c | Duas leituras adicionais autorizadas: flag41 e hitCount. Modo antigo não faz essas leituras; grupo simultâneo usa maior contagem. | Retirar o helper/membro novos apenas na branch DB1. |
| port/src/gs/gs_gl.c, gs_gpu.c, ui.cpp | Toggle próprio F6 e indicador DB1; F7 antigo preservado. | Desligar DB1 ou reverter esses hunks na cópia. |
| .github/workflows/ude-build.yml | Testes DB1/replay e conferência dos símbolos novos no workflow existente. | Reverter somente os passos DB1 desta branch. |
| tools/defense_db/package.py | Pacote novo com as duas assistências desligadas e guia offline. | Usar a pasta original C3, preservada. |
| port/tests/ude_db/* | Unitários, segurança, corpos C nativos, contrato do adaptador e replay que declara dados ausentes. | Arquivos só da candidata; resultados não são homologação. |
| ude_db/IMPLEMENTACAO.md, TESTE-MANUAL.md, tentativas-manuais.csv | Evidências, limites e roteiro com dez tentativas por técnica. | Documentação independente; logs antigos preservados. |

A integração da base oficial 0.1.39.6 ocorreu somente na branch/cópia novas (merge f7e845d). Nenhuma branch ude-integration, ude-v4 ou ude-hybrid-v5 foi reescrita. R7/R9/C3 não foram instaladas por cima. O núcleo ude.c/ude_recipe.c legado não recebeu modificações da DB1. Para reverter a execução basta desligar DB1 ou usar o lançador original; não é necessária restauração dos arquivos congelados.


2026-11-08. Somente artefatos em outputs/ude_db; originais, executáveis, configurações e logs preservados.

| Arquivo | Mudança e motivo | Evidência / reversão |
|---|---|---|
| bt3_defesas_db.json | v1.1.0, 21 revisões por campo, sete janelas, regras anti-mash corrigidas, attack_records por personagem, pendências e procedência | AUDIT.md, audit_matrix.json; substituir pelo backup bt3_defesas_db.original.json |
| attack_records.json / .csv | Dump somente de metadados locais, com comparações independentes | attack_extraction_qa.json; regenerar pelo extrator |
| ude_db_extract_attacks.py | Extrator offline da tabela entry 19 de cada pacote; falha diante de layout/hash divergente | include/battle/btl_tech_a.h e acessores; apagar somente a saída isolada para reverter |
| ude_db_audit.py | Materializa decisões explícitas de revisão; grava apenas nesta pasta | fonte pinado e REVIEW; alterar decisões somente com evidência |
| AUDIT.md / audit_matrix.json / phase1_results.json | Estados e limites reproduzíveis; nenhum sucesso em luta atribuído | Regenerar por ude_db_audit.py |
| protected_files_before.json | Hashes dos originais C3 antes/depois da geração | Somente leitura; nenhuma restauração dos originais é necessária |

Correções por campo no JSON (valores antigos também estão preservados em auditoria.campos):

| Técnica | Campo | Motivo | Evidência |
|---|---|---|---|
| GUARD_HOLD | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_act_h_b.c:BtlDecide_Main:L2165; src/battle/btl_act_c.c:BtlAct_GuardHandler:L774; src/battle/btl_char_hit.c:BtlHit_CheckGuard:L669; src/battle/btl_char_coll_b.c:BtlColl_TryGuard:L1438 |
| GUARD_HIGH_LOW | requisitos | Valores brutos não equivalem sempre ao retorno dos acessores. | src/battle/btl_char_hit.c:BtlHit_CheckGuard:L669; src/battle/btl_tech_a.c:BtlAtk_GetGuardKindA:L1206; src/battle/btl_tech_a.c:BtlAtk_GetGuardKindB:L1218; src/battle/btl_tech_a.c:BtlAtk_GetGuardKindC:L1229 |
| GUARD_HIGH_LOW | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_act_c.c:BtlAct_GuardHandler:L774; src/battle/btl_char_hit.c:BtlHit_CheckGuard:L669; src/battle/btl_tech_a.c:BtlAtk_GetGuardKindA:L1206 |
| VANISH_EVADE | custo | Não transformar o custo do contra em requisito universal da esquiva. | src/battle/btl_char_hit.c:BtlHit_CheckDodge:L499 |
| VANISH_EVADE | direcao | O ramo de duas direções não aceita a opção de trás. | src/battle/btl_char_hit.c:BtlHit_CheckDodge:L499 |
| VANISH_EVADE | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_char_move.c:BtlMove_UpdateDefenseTimers:L1620; src/battle/btl_char_hit.c:BtlHit_CheckDodge:L499; src/battle/btl_act_e.c:BtlAct_VanishStepHandler:L468 |
| Z_COUNTER | requisitos | 0x31 é máscara de bits, não animation ID. Restrição adicional da candidata pode ser conservadora. | src/battle/btl_char_hit.c:BtlHit_CheckGuard:L669 |
| Z_COUNTER | evidencia | Referência incompatível com o layout local. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_char_move.c:BtlMove_UpdateDefenseTimers:L1620; src/battle/btl_char_hit.c:BtlHit_CheckGuard:L669; src/battle/btl_char_hit.c:BtlHit_ApplyGuard:L194 |
| Z_COUNTER | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_char_move.c:BtlMove_UpdateDefenseTimers:L1620; src/battle/btl_char_hit.c:BtlHit_CheckGuard:L669; src/battle/btl_char_hit.c:BtlHit_ApplyGuard:L194 |
| SONIC_SWAY | janela | Manter a postura não mantém indefinidamente Sonic Sway. | src/battle/btl_act_a.c:BtlAct_Action67to69:L2134 |
| SONIC_SWAY | custo | O banco omitia o dreno da postura. | src/battle/btl_act_a.c:BtlAct_Action67to69:L2134; src/battle/btl_char_hit.c:BtlHit_CheckDodge:L499; src/battle/btl_tech_a.c:BtlParam_GetKiDrain67:L1704 |
| SONIC_SWAY | defende | Retirada a afirmação não comprovada de que o resultado depende apenas de chegar durante a entrada. | src/battle/btl_char_hit.c:BtlHit_CheckDodge:L499; src/battle/btl_act_a.c:BtlAct_Action67to69:L2134 |
| SONIC_SWAY | evidencia | Referência incompatível com o layout local. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_act_h_b.c:BtlDecide_Common:L2138; src/battle/btl_act_a.c:BtlAct_Action67to69:L2134; src/battle/btl_char_hit.c:BtlHit_CheckDodge:L499; src/battle/btl_tech_a.c:BtlParam_GetKiDrain67:L1704 |
| SONIC_SWAY | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_act_h_b.c:BtlDecide_Common:L2138; src/battle/btl_act_a.c:BtlAct_Action67to69:L2134; src/battle/btl_char_hit.c:BtlHit_CheckDodge:L499; src/battle/btl_tech_a.c:BtlParam_GetKiDrain67:L1704 |
| VANISHING_STEP | custo | Distinguir limiar de ki para entrar do débito condicional real; não supor gasto incondicional. | src/battle/btl_act_h_b.c:BtlDecide_Main:L2165; src/battle/btl_act_e.c:BtlAct_VanishStepHandler:L468; src/battle/btl_tech_a.c:BtlParam_GetAmountA:L1719 |
| VANISHING_STEP | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_act_h_b.c:BtlDecide_Main:L2165; src/battle/btl_act_e.c:BtlAct_VanishStepHandler:L468; src/battle/btl_tech_a.c:BtlParam_GetAmountA:L1719 |
| VANISH_BEHIND | evidencia | Referência incompatível com o layout local. | src/battle/btl_act_h_b.c:BtlDecide_Main:L2165; src/battle/btl_char_move.c:BtlMove_UpdateDefenseTimers:L1620; src/battle/btl_tech_a.c:BtlParam_GetAmountB:L1731; src/battle/btl_act_e.c:BtlAct_VanishBehindHandler:L590 |
| VANISH_BEHIND | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_act_h_b.c:BtlDecide_Main:L2165; src/battle/btl_char_move.c:BtlMove_UpdateDefenseTimers:L1620; src/battle/btl_tech_a.c:BtlParam_GetAmountB:L1731; src/battle/btl_act_e.c:BtlAct_VanishBehindHandler:L590 |
| BLAST_EVADE | requisitos | Tornar explícito o teste real e não confundir IDs com máscaras. | src/battle/btl_char_coll_b.c:BtlColl_TryDodge:L1081 |
| BLAST_EVADE | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_char_move.c:BtlMove_UpdateDefenseTimers:L1620; src/battle/btl_char_coll_b.c:BtlColl_TryDodge:L1081; src/battle/btl_act_e.c:BtlAct_VanishStepHandler:L468 |
| RUSH_EVADE | requisitos | 0xB1 é máscara e não animation ID; contato nativo continua obrigatório. | src/battle/btl_char_hit.c:BtlHit_CheckRush:L885 |
| RUSH_EVADE | nao_defende | O banco dizia gigantes sem registrar a exceção da técnica. | src/battle/btl_char_hit.c:BtlHit_CheckRush:L885 |
| RUSH_EVADE | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_char_move.c:BtlMove_UpdateDefenseTimers:L1620; src/battle/btl_char_hit.c:BtlHit_CheckRush:L885 |
| THROW_BREAK | requisitos | Dois resolvedores e duas janelas; 0xB4..0xB6 são ações de grab, não attack IDs intercambiáveis. | src/battle/btl_char_hit.c:BtlHit_CheckThrowBreak:L763; src/battle/btl_char_hit.c:BtlHit_CheckRush:L885 |
| THROW_BREAK | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_char_move.c:BtlMove_UpdateDefenseTimers:L1620; src/battle/btl_char_hit.c:BtlHit_CheckThrowBreak:L763; src/battle/btl_char_hit.c:BtlHit_CheckRush:L885 |
| KI_DEFLECT | acao | As ações 0x3C e 0x3D não são equivalentes à postura 0x3B. | src/battle/btl_act_h_b.c:BtlDecide_Main:L2165; src/battle/btl_act_c.c:BtlAct_Action3B:L963; src/battle/btl_act_c.c:BtlAct_Action3C:L1004; src/battle/btl_act_c.c:BtlAct_Action3D:L1031 |
| KI_DEFLECT | custo | O bônus de deflect não se aplica a todos os caminhos antes agrupados. | src/battle/btl_char_coll_b.c:BtlColl_TryDeflect:L1219; src/battle/btl_char_coll_b.c:BtlColl_TryReflect:L1307; src/battle/btl_char_coll_b.c:BtlColl_TryAbsorb:L1359 |
| KI_DEFLECT | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_act_h_b.c:BtlDecide_Main:L2165; src/battle/btl_char_coll_b.c:BtlColl_TryDeflect:L1219; src/battle/btl_act_c.c:BtlAct_Action3B:L963; src/battle/btl_act_c.c:BtlAct_Action3C:L1004; src/battle/btl_act_c.c:BtlAct_Action3D:L1031 |
| PERFECT_DEFLECT | requisitos | Se param_flags & 2, a mesma janela pode seguir absorção perfeita em TryAbsorb, não deflect. | src/battle/btl_char_move.c:BtlMove_UpdateDefenseTimers:L1620; src/battle/btl_char_coll_b.c:BtlColl_TryDeflect:L1219; src/battle/btl_char_coll_b.c:BtlColl_TryAbsorb:L1359 |
| PERFECT_DEFLECT | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_char_move.c:BtlMove_UpdateDefenseTimers:L1620; src/battle/btl_char_coll_b.c:BtlColl_TryDeflect:L1219; src/battle/btl_char_coll_b.c:BtlColl_TryAbsorb:L1359 |
| REFLECT_ABSORB | requisitos | O banco mencionava frente só para reflect e omitia a alternativa perfeita. | src/battle/btl_char_coll_b.c:BtlColl_TryReflect:L1307; src/battle/btl_char_coll_b.c:BtlColl_TryAbsorb:L1359 |
| REFLECT_ABSORB | defende | Absorção pode incluir técnicas elegíveis; os critérios diferem dos de reflexão. | src/battle/btl_char_coll_b.c:BtlColl_TryReflect:L1307; src/battle/btl_char_coll_b.c:BtlColl_TryAbsorb:L1359 |
| REFLECT_ABSORB | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_char_coll_b.c:BtlColl_TryReflect:L1307; src/battle/btl_char_coll_b.c:BtlColl_TryAbsorb:L1359; src/battle/btl_act_c.c:BtlAct_Action3C:L1004; src/battle/btl_act_c.c:BtlAct_Action3D:L1031 |
| SIDESTEP | requisitos | Proximidade não é requisito absoluto de todo chamador; step-in tem capacidade própria. | src/battle/btl_act_h_b.c:BtlDecide_Main:L2165 |
| SIDESTEP | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_act_h_b.c:BtlDecide_Main:L2165; docs/systems/combat.md:ações_1B_1E:L455 |
| DRAGON_DASH_ESCAPE | acao | O pedido/resumo chama 0x1A de homing; o código o distingue de 0x19. | src/battle/btl_act_h_b.c:BtlDecide_Main:L2165 |
| DRAGON_DASH_ESCAPE | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_act_h_b.c:BtlDecide_Main:L2165; docs/systems/combat.md:dash:L454 |
| EMERGENCY_BLASTER_WAVE | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_act_h_b.c:BtlDecide_Main:L2165; src/battle/btl_act_c.c:BtlAct_Action42:L1245; docs/systems/combat.md:ação_42:L554 |
| ARMOR_REPEL | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_char_hit.c:BtlHit_CheckArmor:L645; src/battle/btl_char_hit.c:BtlHit_CheckRepel:L659; src/battle/btl_char_hit.c:resolução_armadura:L1263 |
| SKILL_DEFENSE | defende | Empilhar até três não é incondicional. | src/battle/btl_act_h_b.c:BtlSkill_Apply:L1762; src/battle/btl_char_hit.c:BtlHit_CheckDodge:L499; src/battle/btl_char_coll_b.c:BtlColl_TryDodge:L1081 |
| SKILL_DEFENSE | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_input.c:BtlInput_TestAction:L814; src/battle/btl_act_h_b.c:BtlSkill_Apply:L1762; src/battle/btl_act_j.c:BtlAct_CheckSkillInput:L685; src/battle/btl_char_coll_b.c:BtlColl_TryDodge:L1081; src/battle/btl_char_hit.c:BtlHit_CheckDodge:L499 |
| RECOVERY | evidencia | Referência incompatível com o layout local. | src/battle/btl_act_j.c:BtlAct_CheckRecoveryInput:L1043; src/battle/btl_act_a.c:BtlAct_BlowAwayHandler:L3148; src/battle/btl_act_a.c:BtlAct_LaunchedHandler:L2833; docs/systems/combat.md:reações:L728 |
| RECOVERY | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_act_j.c:BtlAct_CheckRecoveryInput:L1043; src/battle/btl_act_a.c:BtlAct_BlowAwayHandler:L3148; src/battle/btl_act_a.c:BtlAct_LaunchedHandler:L2833; docs/systems/combat.md:reações:L728 |
| CLASH_MASH | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_act_f.c:BtlAct_ClashStruggleHandler:L2975; src/battle/btl_act_e.c:BtlAct_ClashBlowsHandler:L1338; docs/systems/combat.md:Clashes:L238 |
| CLASH_PROMPT | confianca | A confiança global original misturava mecânica, nomes e listas comunitárias. Confiança agora por campo; nenhum novo cotejo MIPS foi executado. | src/battle/btl_act_e.c:BtlAct_ClashExchangeHandler:L1502; docs/systems/combat.md:Clash_C:L473 |

Janelas e regras gerais: ver matriz em AUDIT.md; LOCKON explícito 0x4000, BLAST2 explícito 0x01000000, custos de postura e limiar/gasto de passo corrigidos. Reversão do banco inteiro: utilizar o anexo preservado em bt3_defesas_db.original.json.

Não há commit, branch nova, merge, edição de SQLite original, modificação da C3, build x64 ou deploy nesta fase.

## Fechamento do build final — 2026-11-09

- `ude_db/BUILD.json`: identifica o build funcional `96cac4b`, run `42592500592`, hashes de artefato e executável, arquitetura Windows x64 e ausência de homologação. Evidência: artefatos/logs do workflow `ude-build`.
- `ude_db/RELATORIO.md`, `LEIA-PRIMEIRO.md` e `PRESERVACAO.json`: registram resultados, uso do pacote separado e preservação dos 77 arquivos C3 e das três branches legadas. O commit de fechamento altera somente documentação; o binário corresponde ao commit funcional acima.
- Reversão: remover estes documentos ou reverter somente o commit de fechamento. Não altera código compilado, dados do jogo ou a C3.
