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
A compilação passou em Windows/Linux/macOS na execução 39361409444. No Windows,
o usuário confirmou áudio audível após marcar os passos 1, 5, 9 e 13, ativar
Pattern 1 nos dois primeiros compassos e iniciar o transporte. O playhead
avançou e Stop voltou à posição zero. Falta testar áudio importado nesta versão;
não há suíte automatizada de áudio neste marco.

## Próxima prioridade

Persistência do pattern e de clipes com posição e duração; em seguida,
transporte único e edição de clipes de áudio. Isso permitirá salvar e reabrir
um arranjo antes de expandir para MIDI, instrumentos e plugins.

## Revisão visual em andamento (20/10/2026)

O FL Studio 2025 instalado foi observado diretamente na mesma tela que o PULSE:
toolbar de duas faixas compactas, Browser à esquerda, Playlist ocupando o centro
com oito ou mais trilhas visíveis e Channel Rack sobreposto/alternável. A revisão
de `Main.cpp` reorganiza os controles existentes nesse sentido: toolbar menor,
Browser com os arquivos importados, cabeçalhos de trilha, régua e grid de oito
compassos, clipes compactos, playhead contínuo e Channel Rack que abre/fecha.
Os mesmos botões de steps, pattern, transporte e importação seguem conectados
à lógica de áudio anterior. Esta revisão ainda requer compilação e teste visual
do novo executável; os clipes de áudio ainda não têm waveform nem edição.
