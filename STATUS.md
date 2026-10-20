# Estado dos subsistemas

Revisado em 19/10/2026 a partir de `Main.cpp`, `CMakeLists.txt`, `README.md`,
do histórico `main` e da compilação inicial nos três sistemas.

| Subsistema | Estado observado |
| --- | --- |
| Playlist, timeline e clipes | Barras de duração e régua 4/4 no mesmo `Studio`; ainda não há clipes posicionáveis, corte ou formas de onda. |
| Reprodução e sincronização | MP3/WAV em até oito `AudioTransportSource` misturados por `MixerAudioSource`; play/pause/stop e busca aplicada a todas as pistas. Os transportes são iniciados em sequência, sem relógio comum por amostra. |
| BPM, metrônomo e transporte | BPM 40–240 usado na régua visual. Não altera áudio, não há mapa de tempo nem metrônomo. |
| Channel Rack e patterns | Ausentes. |
| Piano Roll e MIDI | Ausentes. |
| Sampler e instrumentos | Ausentes; a importação atual toca fontes de áudio lineares. |
| Mixer, roteamento e efeitos | Volume, mute e solo por pista com ganho no transporte; mistura estéreo ao dispositivo padrão. Não há buses, panorama, efeitos ou plugins. |
| Projetos e exportação | Ausentes. Os arquivos de origem só vivem na sessão atual. |

## Integração atual

Os botões e controles JUCE vivem no `Studio`. Cada pista mantém um leitor de
arquivo e um `AudioTransportSource` alimentado por leitura antecipada em disco.
O `MixerAudioSource` fornece blocos para o callback de áudio do
`AudioAppComponent`. Os controles de volume, mute e solo atualizam o ganho
dos transportes reais. A busca envia a posição escolhida a cada transporte.

## Entrega desta etapa

Continuação de `Main.cpp`: controles de play/pause, busca, ganho, mute, solo e
marcação 4/4, com documentação atualizada. Não substitui o motor ou a interface.
A validação necessária é a compilação Windows/Linux/macOS e teste em execução
com arquivos de áudio reais. Não há suíte automatizada de áudio neste marco.

## Próxima prioridade

Modelo persistente de projeto e clipe com posição e duração; em seguida,
transporte único e edição de clipes. Isso permitirá salvar e reabrir um arranjo
antes de expandir para MIDI, instrumentos e plugins.
