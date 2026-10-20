# Estado dos subsistemas

Revisado em 19/10/2026 a partir de `Main.cpp`, `CMakeLists.txt`, `README.md`,
do histórico `main` e da compilação inicial nos três sistemas.

| Subsistema | Estado observado |
| --- | --- |
| Playlist, timeline e clipes | Instâncias ligáveis de Pattern 1 em oito compassos, além das barras de áudio existentes. Sem arrastar/aparar/dividir clipes ou formas de onda. |
| Reprodução e sincronização | MP3/WAV em até oito `AudioTransportSource` misturados por `MixerAudioSource`, mais um sequenciador interno no callback. Os transportes de arquivos ainda são iniciados em sequência, sem relógio comum por amostra. |
| BPM, metrônomo e transporte | BPM 40–240 controla régua e sequenciador de passos. Não estica MP3/WAV, não há mapa de tempo nem metrônomo. |
| Channel Rack e patterns | Um channel de bumbo sintetizado, 16 passos e um pattern repetível em oito posições. Sem múltiplos canais/patterns. |
| Piano Roll e MIDI | Ausentes. |
| Sampler e instrumentos | Bumbo interno sintetizado; sem sampler de arquivos ou instrumentos melódicos. |
| Mixer, roteamento e efeitos | Volume, mute e solo por pista com ganho no transporte; mistura estéreo ao dispositivo padrão. Não há buses, panorama, efeitos ou plugins. |
| Projetos e exportação | Ausentes. Os arquivos de origem só vivem na sessão atual. |

## Integração atual

Os botões e controles JUCE vivem no `Studio`. Cada pista mantém um leitor de
arquivo e um `AudioTransportSource` alimentado por leitura antecipada em disco.
O `MixerAudioSource` fornece blocos para o callback de áudio do
`AudioAppComponent`. O sequenciador soma o bumbo sintetizado ao mesmo bloco;
máscaras atômicas levam os passos e posições marcados na interface ao callback.
Os controles de volume, mute e solo atualizam o ganho dos transportes de arquivo.
A busca envia a posição escolhida a cada transporte e ao sequenciador.

## Entrega desta etapa

Após observar Playlist e Channel Rack do FL Studio 2025 em um experimento
temporário, `Main.cpp` ganhou um pattern audível próprio com 16 passos e oito
posições na Playlist. É uma extensão do callback existente, não um motor paralelo.
A validação necessária é a compilação Windows/Linux/macOS e teste em execução.
Não há suíte automatizada de áudio neste marco.

## Próxima prioridade

Persistência do pattern e de clipes com posição e duração; em seguida,
transporte único e edição de clipes de áudio. Isso permitirá salvar e reabrir
um arranjo antes de expandir para MIDI, instrumentos e plugins.
