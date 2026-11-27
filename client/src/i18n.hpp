#pragma once
#include <cstddef>
#include <cstdio>
#include <string>

namespace i18n {
enum class Lang { EN, PT, ES, AR, Count };
inline const char* code(Lang lang) { return lang == Lang::AR ? "ar" : lang == Lang::PT ? "pt" : lang == Lang::ES ? "es" : "en"; }

inline Lang language(const std::string& code) { return code == "ar" ? Lang::AR : code == "pt" ? Lang::PT : code == "es" ? Lang::ES : Lang::EN; }
inline Lang cycle(Lang lang, int direction) { return Lang((int(lang)+direction+int(Lang::Count))%int(Lang::Count)); }

inline std::string shortDate(const std::string& iso, Lang lang) {
  int year, month, day;
  if (iso.size() != 10 || std::sscanf(iso.c_str(), "%4d-%2d-%2d", &year, &month, &day) != 3 ||
      month < 1 || month > 12 || day < 1 || day > 31) return iso;
  static const char* const months[][12] = {
    {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"},
    {"jan","fev","mar","abr","mai","jun","jul","ago","set","out","nov","dez"},
    {"ene","feb","mar","abr","may","jun","jul","ago","sep","oct","nov","dic"},
    {"يناير","فبراير","مارس","أبريل","مايو","يونيو","يوليو","أغسطس","سبتمبر","أكتوبر","نوفمبر","ديسمبر"}
  };
  char date[32];
  if (lang != Lang::EN) std::snprintf(date, sizeof(date), "%d %s %d", day, months[lang == Lang::AR ? 3 : lang == Lang::ES ? 2 : 1][month-1], year);
  else std::snprintf(date, sizeof(date), "%s %d, %d", months[0][month-1], day, year);
  return date;
}

// Texto do app. Títulos, nomes e metadados do servidor continuam sendo dados.
#define TUBE360_TEXT(X) \
  X(None, "", "", "") \
  X(LauncherOpening, "Tube360 – opening…", "Tube360 – abrindo…", "Tube360 – abriendo…") \
  X(UpdateAvailable, "Update · ", "Atualização · ", "Actualización · ") \
  X(Updates, "App updates", "Atualizações do app", "Actualizaciones de la app") \
  X(UpdateInstalled, "Installed · ", "Instalada · ", "Instalada · ") \
  X(UpdateCurrent, "No newer signed version found. Menu checks again.", "Nenhuma versão assinada mais recente encontrada. Menu verifica de novo.", "No se encontró una versión firmada más reciente. Menu vuelve a comprobar.") \
  X(UpdateChecking, "Checking for signed updates…", "Verificando atualizações assinadas…", "Buscando actualizaciones firmadas…") \
  X(UpdateCheckFailed, "Could not verify updates. Menu tries again.", "Não foi possível verificar atualizações. Menu tenta de novo.", "No se pudieron verificar las actualizaciones. Menu reintenta.") \
  X(UpdateDownload, "Update", "Atualizar", "Actualizar") \
  X(UpdateDownloading, "Downloading and checking…", "Baixando e conferindo…", "Descargando y verificando…") \
  X(UpdateNextStart, "The update will be installed the next time you open Tube360.", "A atualização será instalada na próxima abertura do Tube360.", "La actualización se instalará la próxima vez que abras Tube360.") \
  X(UpdateReady, "Update ready · reopen Tube360", "Atualização pronta · reabra o Tube360", "Actualización lista · vuelve a abrir Tube360") \
  X(UpdateFailed, "Could not verify or save the update. Try again.", "Não foi possível conferir ou salvar. Tente novamente.", "No se pudo verificar o guardar la actualización. Inténtalo de nuevo.") \
  X(UpdateSetup, "Install the complete app package once to enable automatic updates.", "Instale o pacote completo uma vez para ativar as atualizações.", "Instala el paquete completo una vez para activar las actualizaciones.") \
  X(ServerOriginal, "Server · original", "Servidor · original", "Servidor · original") \
  X(ServerConverted, "Server · converted", "Servidor · convertido", "Servidor · convertido") \
  X(ConsoleVideoError, "Could not open the YouTube stream on this console.", "O console não conseguiu abrir o fluxo do YouTube.", "No se pudo abrir el video de YouTube en esta consola.") \
  X(Console, "Console", "Console", "Consola") \
  X(ServerMode, "Server", "Servidor", "Servidor") \
  X(Automatic, "Automatic", "Automático", "Automático") \
  X(Video720Fast, "720p · 60 fps", "720p · 60 fps", "720p · 60 fps") \
  X(Video1080Detail, "1080p · 30 fps", "1080p · 30 fps", "1080p · 30 fps") \
  X(VideoAdjusted, "Adjusted to keep playback smooth", "Ajustado para manter a reprodução fluida", "Ajustado para mantener una reproducción fluida") \
  X(DisplayOutput, "TV output", "Saída da TV", "Salida de TV") \
  X(Display1080Help, "For native 1080p, select 1080p (progressive) in Xbox Display Settings and use HDMI.", "Para 1080p nativo, selecione 1080p progressivo no painel do Xbox e use HDMI.", "Para 1080p nativo, selecciona 1080p progresivo en la configuración de pantalla de Xbox y usa HDMI.") \
  X(SafeOutputHelp, "This video mode keeps the current output. Picture fit and text size remain available.", "Este modo de vídeo mantém a saída atual. Ajustar a imagem e o texto continua disponível.", "Este modo de vídeo mantiene la salida actual. El ajuste de imagen y texto sigue disponible.") \
  X(AudioTrack, "Audio", "Áudio", "Audio") \
  X(AudioPreference, "Audio preference", "Preferência de áudio", "Preferencia de audio") \
  X(AudioAlwaysOriginal, "Always original", "Sempre o original", "Siempre original") \
  X(AudioInterface, "Interface language when available", "Idioma da interface quando houver", "Idioma de la interfaz, si está disponible") \
  X(AudioOriginal, "Original", "Original", "Original") \
  X(AudioAutoDub, "automatically dubbed", "dublado automaticamente", "doblaje automático") \
  X(AudioDub, "dubbed", "dublado", "doblado") \
  X(AudioEnglish, "English", "inglês", "inglés") \
  X(AudioPortuguese, "Portuguese", "português", "portugués") \
  X(AudioSpanish, "Spanish", "espanhol", "español") \
  X(AudioFrench, "French", "francês", "francés") \
  X(AudioGerman, "German", "alemão", "alemán") \
  X(AudioJapanese, "Japanese", "japonês", "japonés") \
  X(AudioUnknown, "Audio track", "Faixa de áudio", "Pista de audio") \
  X(Home, "Home", "Início", "Inicio") \
  X(Search, "Search", "Buscar", "Buscar") \
  X(Phone, "Phone", "Celular", "Teléfono") \
  X(Settings, "Settings", "Configurações", "Configuración") \
  X(SponsorBlock, "SponsorBlock", "SponsorBlock", "SponsorBlock") \
  X(SponsorOff, "Off", "Desligado", "Desactivado") \
  X(SponsorButton, "Skip button (default)", "Botão \"Pular\" (padrão)", "Botón \"Saltar\" (predeterminado)") \
  X(SponsorAuto, "Skip automatically", "Pular sozinho", "Saltar automáticamente") \
  X(SponsorWarning, "Segments are marked by the community and may be wrong.", "Os trechos são marcados pela comunidade e podem errar.", "Los segmentos los marca la comunidad y pueden contener errores.") \
  X(SponsorCredit, "Segments: SponsorBlock, sponsor.ajay.app", "Trechos: SponsorBlock, sponsor.ajay.app", "Segmentos: SponsorBlock, sponsor.ajay.app") \
  X(SponsorCategories, "Sponsors, self-promotion and subscribe reminders. Intros and outros stay on.", "Patrocínio, autopromoção e lembretes de inscrição. Vinhetas e encerramentos continuam.", "Patrocinios, autopromoción y recordatorios de suscripción. Se conservan intros y cierres.") \
  X(SponsorSkip, "Skip sponsor", "Pular patrocínio", "Saltar patrocinio") \
  X(SponsorSkipped, "Sponsor skipped", "Patrocínio pulado", "Patrocinio saltado") \
  X(SponsorUndo, "Go back", "Voltar", "Volver") \
  X(Exit, "Exit", "Sair", "Salir") \
  X(All, "All", "Tudo", "Todo") \
  X(ForYou, "For you", "Para você", "Para ti") \
  X(Subscriptions, "Subscriptions", "Inscrições", "Suscripciones") \
  X(ContinueWatching, "Continue watching", "Continuar assistindo", "Seguir viendo") \
  X(WatchLater, "Watch later", "Assistir mais tarde", "Ver más tarde") \
  X(Music, "Music", "Música", "Música") \
  X(Football, "Football", "Futebol", "Fútbol") \
  X(Games, "Games", "Games", "Videojuegos") \
  X(Podcasts, "Podcasts", "Podcasts", "Podcasts") \
  X(News, "News", "Notícias", "Noticias") \
  X(MusicQuery, "popular music hits", "músicas brasileiras mais tocadas", "éxitos de música en español") \
  X(FootballQuery, "football match highlights", "melhores momentos futebol brasileiro", "resúmenes de partidos de fútbol") \
  X(GamesQuery, "gameplay gaming", "gameplay pt br", "videojuegos en español") \
  X(PodcastsQuery, "podcast highlights", "cortes podcast", "fragmentos de podcasts en español") \
  X(NewsQuery, "today world news", "notícias de hoje brasil", "noticias de hoy") \
  X(Keyboard1, "abcdefghij", "abcdefghij", "abcdefghij") \
  X(Keyboard2, "klmnopqrst", "klmnopqrst", "klmnopqrst") \
  X(Keyboard3, "uvwxyz&-'.", "uvwxyzç-'.", "uvwxyzñ-'.") \
  X(Keyboard4, "1234567890", "1234567890", "1234567890") \
  X(Refresh, "Refresh", "Atualizar", "Actualizar") \
  X(Refreshing, "Finding new videos…", "Buscando vídeos novos…", "Buscando videos nuevos…") \
  X(LoadingVideos, "Loading videos…", "Carregando vídeos…", "Cargando videos…") \
  X(NoResponse, "No response from the server", "Sem resposta do servidor", "El servidor no responde") \
  X(ClockWrong, "Set the console date and time", "Acerte a data e a hora do console", "Ajusta la fecha y la hora de la consola") \
  X(ConsoleClock, "Console clock: ", "Relógio do console: ", "Reloj de la consola: ") \
  X(TLSFailed, "Secure connection failed. Check for app updates.", "Falha na conexão segura. Confira as atualizações do app.", "Falló la conexión segura. Busca actualizaciones de la app.") \
  X(DNSFailed, "DNS failed. Check your network DNS settings.", "Falha no DNS. Confira o DNS nas configurações de rede.", "Falló el DNS. Revisa el DNS en los ajustes de red.") \
  X(NetworkFailed, "No connection. Check your cable or Wi-Fi.", "Sem conexão. Confira o cabo ou Wi-Fi.", "Sin conexión. Revisa el cable o Wi-Fi.") \
  X(YouTubeRefused, "YouTube refused the request. Try again later.", "YouTube recusou o pedido. Tente mais tarde.", "YouTube rechazó la solicitud. Inténtalo más tarde.") \
  X(StartupTitle, "Tube360 could not start", "Tube360 não conseguiu iniciar", "Tube360 no pudo iniciar") \
  X(StartupSDL, "Controller or audio initialization failed.", "Falha ao iniciar o controle ou áudio.", "Falló el inicio del mando o audio.") \
  X(StartupVideo, "Video or GPU initialization failed.", "Falha ao iniciar o vídeo ou GPU.", "Falló el inicio del vídeo o GPU.") \
  X(StartupFonts, "Missing or invalid fonts:", "Fontes ausentes ou inválidas:", "Fuentes ausentes o no válidas:") \
  X(StartupCopy, "Extract the complete app folder again. Keep all eight fonts beside default.xex and tube360.xex.", "Extraia a pasta completa do app de novo. Mantenha as oito fontes junto de default.xex e tube360.xex.", "Extrae de nuevo la carpeta completa. Deja las ocho fuentes junto a default.xex y tube360.xex.") \
  X(StartupRetry, "Restart the console and try again. If it persists, copy the complete app folder again.", "Reinicie o console e tente de novo. Se persistir, copie a pasta completa do app outra vez.", "Reinicia la consola y reintenta. Si persiste, copia de nuevo la carpeta completa de la app.") \
  X(StartupFolder, "App folder is not writable.", "Não é possível gravar na pasta do app.", "No se puede escribir en la carpeta de la app.") \
  X(StartupStorage, "Free storage space and check the app folder permissions.", "Libere espaço e confira as permissões da pasta do app.", "Libera espacio y revisa los permisos de la carpeta de la app.") \
  X(StartupLaunch, "Could not open tube360.xex.", "Não foi possível abrir tube360.xex.", "No se pudo abrir tube360.xex.") \
  X(StartupTimeout, "The app did not start within 20 seconds.", "O app não iniciou em 20 segundos.", "La app no inició en 20 segundos.") \
  X(StartupExit, "Exit", "Sair", "Salir") \
  X(SeekTarget, "Seek to ", "Ir para ", "Ir a ") \
  X(BadResponse, "Could not load this list", "Não deu para carregar esta lista", "No se pudo cargar esta lista") \
  X(Empty, "Nothing found.", "Nada encontrado.", "No se encontraron resultados.") \
  X(PageError, "Could not load more videos. Press Menu to refresh.", "Não deu para carregar mais vídeos. Aperte Menu para atualizar.", "No se pudieron cargar más videos. Presiona Menu para actualizar.") \
  X(Live, "LIVE", "AO VIVO", "EN VIVO") \
  X(PhonePrefix, "Phone: ", "Celular: ", "Teléfono: ") \
  X(AccountPrefix, "Account: ", "Conta: ", "Cuenta: ") \
  X(SearchPrompt, "What do you want to watch?", "O que você quer assistir?", "¿Qué quieres ver?") \
  X(RecentSearches, "Recent searches", "Buscas recentes", "Búsquedas recientes") \
  X(RecentEmpty, "Your searches will appear here.", "O que você buscar aparece aqui.", "Tus búsquedas aparecerán aquí.") \
  X(Type, "Type", "Digitar", "Escribir") \
  X(Delete, "Delete", "Apagar", "Borrar") \
  X(Space, "Space", "Espaço", "Espacio") \
  X(Back, "Back", "Voltar", "Volver") \
  X(Close, "Close", "Fechar", "Cerrar") \
  X(Resume, "Resume", "Continuar", "Continuar") \
  X(Pause, "Pause", "Pausar", "Pausar") \
  X(Original, "original", "original", "original") \
  X(Converted, "converted", "convertido", "convertido") \
  X(Q720, "720p", "720p", "720p") \
  X(Q480, "480p", "480p", "480p") \
  X(Q360, "360p", "360p", "360p") \
  X(QualityPrefix, "Quality: ", "Qualidade: ", "Calidad: ") \
  X(Video480, "480p", "480p", "480p") \
  X(Video360, "360p", "360p", "360p") \
  X(Video240, "240p", "240p", "240p") \
  X(Video144, "144p", "144p", "144p") \
  X(ManualQualityHelp, "Manual quality stays fixed. Automatic profiles adapt to playback.", "A qualidade manual fica fixa. Os perfis automáticos se adaptam à reprodução.", "La calidad manual queda fija. Los perfiles automáticos se adaptan a la reproducción.") \
  X(ManualQualityPressure, "This quality is struggling. Choose a lower quality or Automatic.", "Esta qualidade está travando. Escolha uma menor ou Automático.", "Esta calidad se está trabando. Elige una menor o Automático.") \
  X(ManualQualityMissing, "This quality is unavailable. Open Quality to choose another.", "Esta qualidade não está disponível. Abra Qualidade e escolha outra.", "Esta calidad no está disponible. Abre Calidad y elige otra.") \
  X(ManualQualityFailed, "Could not keep this quality. Open Quality to choose another.", "Não foi possível manter esta qualidade. Abra Qualidade e escolha outra.", "No se pudo mantener esta calidad. Abre Calidad y elige otra.") \
  X(SoundPrefix, "Sound: ", "Som: ", "Sonido: ") \
  X(TV, "TV", "TV", "TV") \
  X(Nest, "Nest", "Nest", "Nest") \
  X(SyncPrefix, "Speaker sync: ", "Sincronia da caixa: ", "Sincronización del altavoz: ") \
  X(Milliseconds, " ms", " ms", " ms") \
  X(TenSeconds, "10 s", "10 s", "10 s") \
  X(OneMinute, "1 min", "1 min", "1 min") \
  X(SpeakerFallback, "Speaker did not respond: using TV sound (X to retry)", "A caixa não respondeu: som na TV (X tenta de novo)", "El altavoz no responde: sonido en la TV (X para reintentar)") \
  X(RequestingVideo, "Requesting video from the server…", "Pedindo o vídeo ao servidor…", "Solicitando el video al servidor…") \
  X(Loading, "Loading…", "Carregando…", "Cargando…") \
  X(VideoFailed, "Could not play this video", "Não deu para tocar este vídeo", "No se pudo reproducir este video") \
  X(VideoOpenError, "Couldn't open this video.", "Não deu para abrir este vídeo.", "No se pudo abrir este video.") \
  X(Rate, "Rate", "Avaliar", "Calificar") \
  X(Comments, "Comments", "Comentários", "Comentarios") \
  X(CommentsEmpty, "Comments are unavailable for this video.", "Comentários indisponíveis para este vídeo.", "Los comentarios no están disponibles para este video.") \
  X(CommentsFailed, "Could not load comments. Press A to retry.", "Não deu para carregar comentários. Aperte A para tentar de novo.", "No se pudieron cargar los comentarios. Presiona A para reintentar.") \
  X(CommentsMore, "More comments", "Mais comentários", "Más comentarios") \
  X(Scroll, "Scroll", "Rolar", "Desplazar") \
  X(PlaybackSpeed, "Playback speed", "Velocidade", "Velocidad de reproducción") \
  X(SpeedLive, "Unavailable during live broadcasts", "Indisponível em transmissões ao vivo", "No disponible en transmisiones en vivo") \
  X(SpeedTV, "Changed speed: sound on the TV.", "Velocidade alterada: som na TV.", "Velocidad cambiada: sonido en la TV.") \
  X(SpeedNest, "Select 1x to use the speaker.", "Selecione 1x para usar a caixa.", "Selecciona 1x para usar el altavoz.") \
  X(RepeatVideo, "Repeat video", "Repetir vídeo", "Repetir video") \
  X(RepeatOn, "On", "Ligado", "Activado") \
  X(RepeatOff, "Off", "Desligado", "Desactivado") \
  X(DislikeEstimate, "Estimate · returnyoutubedislike.com", "Estimativa · returnyoutubedislike.com", "Estimación · returnyoutubedislike.com") \
  X(Members, "Members", "Membros", "Miembros") \
  X(Member, "Member", "Membro", "Miembro") \
  X(RateTitle, "Rate this video", "Avaliar este vídeo", "Calificar este video") \
  X(Like, "Like", "Gostei", "Me gusta") \
  X(Dislike, "Dislike", "Não gostei", "No me gusta") \
  X(Liked, "You liked this video", "Você marcou que gostou", "Marcaste que te gusta este video") \
  X(Disliked, "You disliked this video", "Você marcou que não gostou", "Marcaste que no te gusta este video") \
  X(RatingRemoved, "Rating removed", "Avaliação removida", "Calificación eliminada") \
  X(RateFailed, "Could not save your rating", "Não deu para salvar a avaliação", "No se pudo guardar tu calificación") \
  X(RateSignIn, "Sign in to YouTube in Settings to rate videos", "Conecte sua conta do YouTube nas configurações para avaliar", "Inicia sesión en YouTube desde Configuración para calificar") \
  X(UpNextIn, "Up next in ", "A seguir em ", "A continuación en ") \
  X(WatchNow, "Watch now", "Assistir agora", "Ver ahora") \
  X(VideoMembers, "This video is only for channel members", "Este vídeo é só para membros do canal", "Este video es exclusivo para miembros del canal") \
  X(VideoPrivate, "This video is private", "Este vídeo é privado", "Este video es privado") \
  X(VideoAge, "YouTube asks to confirm your age for this video", "O YouTube pede confirmação de idade para este vídeo", "YouTube solicita confirmar tu edad para ver este video") \
  X(VideoGone, "This video is no longer available on YouTube", "Este vídeo não está mais disponível no YouTube", "Este video ya no está disponible en YouTube") \
  X(SpeakerError, "The speaker could not start playback", "A caixa não conseguiu começar a tocar", "El altavoz no pudo iniciar la reproducción") \
  X(PhoneRequest, "Requested from your phone", "Pedido do celular", "Solicitado desde tu teléfono") \
  X(PhoneConnected, "Phone connected: ", "Celular conectado: ", "Teléfono conectado: ") \
  X(PhoneDisconnected, "Phone disconnected", "O celular desconectou", "El teléfono se desconectó") \
  X(PhoneUpdated, "Phone connection updated", "Conexão do celular atualizada", "Conexión del teléfono actualizada") \
  X(CastTitle, "Cast from your phone", "Transmitir do celular", "Transmitir desde tu teléfono") \
  X(CastChoose, "In the YouTube app, tap Cast and choose ", "No app do YouTube, toque em Transmitir e escolha ", "En la app de YouTube, toca Transmitir y elige ") \
  X(CastPath, "Or: your profile > Settings > Watch on TV > Enter TV code", "Ou: sua foto > Configurações > Assistir na TV > Inserir código da TV", "O bien: tu perfil > Configuración > Ver en TV > Ingresar código de TV") \
  X(CastOffline, "Cast is unavailable. Check the network connection.", "Transmitir indisponível. Verifique a conexão de rede.", "Transmitir no está disponible. Revisa la conexión de red.") \
  X(CastLoading, "Getting the TV code…", "Buscando o código…", "Obteniendo el código de TV…") \
  X(CastRemember, "The code changes every 5 minutes. Pair once and your phone remembers this TV.", "O código muda a cada 5 minutos. Pareie uma vez e o celular lembra desta TV.", "El código cambia cada 5 minutos. Vincula tu teléfono una vez para recordar esta TV.") \
  X(NoPhone, "No phone connected yet.", "Nenhum celular conectado ainda.", "Todavía no hay un teléfono conectado.") \
  X(SearchSpeakers, "Find speakers", "Procurar alto-falantes", "Buscar altavoces") \
  X(SpeakerSearchHelp, "Choose a speaker on your local network.", "Escolha um alto-falante na sua rede local.", "Elige un altavoz en tu red local.") \
  X(SpeakerSearching, "Searching your network...", "Procurando na sua rede...", "Buscando en tu red...") \
  X(SpeakerNone, "None (TV sound)", "Nenhum (som na TV)", "Ninguno (sonido en la TV)") \
  X(SpeakerEmpty, "No speakers found. Check that they are on the same network.", "Nenhum alto-falante encontrado. Confira se está na mesma rede.", "No se encontraron altavoces. Comprueba que estén en la misma red.") \
  X(SpeakerNetworkError, "Could not read the local network. Check the console connection.", "Não deu para ler a rede local. Confira a conexão do console.", "No se pudo leer la red local. Comprueba la conexión de la consola.") \
  X(SpeakerIncomplete, "Search incomplete. Try again to find all speakers.", "Busca incompleta. Tente novamente para encontrar todos os alto-falantes.", "Búsqueda incompleta. Intenta de nuevo para encontrar todos los altavoces.") \
  X(SpeakerCancelled, "Search cancelled", "Busca cancelada", "Búsqueda cancelada") \
  X(SpeakerSelected, "Selected", "Selecionado", "Seleccionado") \
  X(SpeakerSearchAgain, "Search again", "Procurar de novo", "Buscar de nuevo") \
  X(SpeakerChoose, "Select", "Escolher", "Elegir") \
  X(SettingsIntro, "Make yourself at home.", "Do seu jeito, no seu console.", "A tu gusto, en tu consola.") \
  X(YouTubeAccount, "YouTube account", "Conta do YouTube", "Cuenta de YouTube") \
  X(VideosPerRow, "Videos per row", "Vídeos por linha", "Videos por fila") \
  X(DefaultQuality, "Default quality", "Qualidade padrão", "Calidad predeterminada") \
  X(Sound, "Sound", "Som", "Sonido") \
  X(SpeakerSync, "Speaker sync", "Sincronia da caixa", "Sincronización del altavoz") \
  X(SleepTimer, "Sleep timer", "Timer de desligar", "Temporizador de apagado") \
  X(SleepTogether, "Also turn off", "Desligar junto", "Apagar también") \
  X(SleepOff, "Off", "Desligado", "Desactivado") \
  X(SleepEnd, "At end of video", "No fim do vídeo", "Al terminar el video") \
  X(SleepXbox, "Xbox only", "Só o Xbox", "Solo Xbox") \
  X(SleepNotebook, "Xbox and notebook", "Xbox e notebook", "Xbox y computadora") \
  X(SleepTVHelp, "Samsung TV turns off automatically with 'No Signal Power Off' enabled.", "A TV Samsung desliga sozinha se 'Desligamento sem sinal' estiver ligado.", "La TV Samsung se apaga sola si activas el apagado por falta de señal.") \
  X(SleepWarning, "Shutting down in ", "Desligando em ", "Se apagará en ") \
  X(SleepSeconds, " s", " s", " s") \
  X(SleepAdd, "+15 min", "+15 min", "+15 min") \
  X(SleepShuttingDown, "Shutting down…", "Desligando…", "Apagando…") \
  X(SleepError, "Could not update sleep timer. Try again.", "Não deu para alterar o timer. Tente de novo.", "No se pudo cambiar el temporizador. Inténtalo de nuevo.") \
  X(SleepApply, "Apply", "Aplicar", "Aplicar") \
  X(PlayerOptions, "Options", "Opções", "Opciones") \
  X(PlayerTimeline, "Timeline", "Trilha do vídeo", "Barra de reproducción") \
  X(PlayerQuality, "Quality", "Qualidade", "Calidad") \
  X(PlayerChannel, "Go to channel", "Ir para o canal", "Ir al canal") \
  X(PlayerPaused, "Paused", "Pausado", "En pausa") \
  X(PlayerSeekWait, "Seeking is available after the video starts", "Você pode pular depois que o vídeo começar", "Podrás avanzar cuando comience el video") \
  X(PlayerLiveSeek, "Live broadcast: seeking is unavailable", "Transmissão ao vivo: não dá para pular", "No se puede avanzar en esta transmisión en vivo") \
  X(Captions, "Captions", "Legendas", "Subtítulos") \
  X(CaptionsOn, "On", "Ligadas", "Activados") \
  X(CaptionsOff, "Off", "Desligadas", "Desactivados") \
  X(CaptionsAuto, "Automatic", "Automáticas", "Automáticos") \
  X(CaptionsManual, "Creator captions", "Do criador", "Del creador") \
  X(CaptionsLoading, "Finding captions…", "Buscando legendas…", "Buscando subtítulos…") \
  X(CaptionsNone, "This video has no captions", "Este vídeo não oferece legendas", "Este video no tiene subtítulos") \
  X(CaptionsLiveNone, "YouTube has no captions for this live broadcast", "O YouTube não oferece legendas nesta live", "YouTube no ofrece subtítulos para esta transmisión") \
  X(CaptionsFailed, "Could not load captions. Press A to retry.", "Não deu para carregar legendas. Aperte A para tentar de novo.", "No se pudieron cargar los subtítulos. Presiona A para reintentar.") \
  X(CaptionsLanguage, "Caption language", "Idioma das legendas", "Idioma de los subtítulos") \
  X(PlayerAlsoOff, "Also turn off", "Também desligar", "Apagar también") \
  X(PlayerTimerOn, "Active", "Ativo", "Activo") \
  X(CaptionsLiveClock, "Captions for this broadcast are unavailable in the player", "As legendas desta live estão indisponíveis no player", "Los subtítulos de esta transmisión no están disponibles en el reproductor") \
  X(Spanish, "Español", "Español", "Español") \
  X(LargeText, "Large text", "Texto grande", "Texto grande") \
  X(Enabled, "Enabled", "Ativado", "Activado") \
  X(LargeTextHelp, "Larger text and fewer columns. Always enabled on SD TVs.", "Textos maiores e menos colunas. Sempre ativo em TVs SD.", "Texto más grande y menos columnas. Siempre activo en TV SD.") \
  X(VideoFit, "Picture size", "Tamanho da imagem", "Tamaño de imagen") \
  X(VideoFitLetterbox, "Fit (letterbox)", "Ajustar (faixas)", "Ajustar (con bandas)") \
  X(VideoFitFill, "Fill (crop)", "Preencher (cortar)", "Llenar (recortar)") \
  X(DisplaySDHelp, "480i/p follows Xbox Display Settings. SD keeps a safe TV margin.", "480i/p segue o painel do Xbox. SD reserva margem segura na TV.", "480i/p sigue la pantalla de Xbox. SD deja un margen seguro en la TV.") \
  X(Two, "2 (4:3)", "2 (4:3)", "2 (4:3)") \
  X(Language, "Language", "Idioma", "Idioma") \
  X(English, "English", "English", "English") \
  X(KeyboardToggle, "Arabic / Latin", "Árabe / Latim", "Árabe / Latín") \
  X(ArabicFontMissing, "Arabic fonts missing. Copy noto-sans-arabic-300/400/600/700.ttf beside default.xex and tube360.xex.", "Fontes árabes ausentes. Copie noto-sans-arabic-300/400/600/700.ttf junto de default.xex e tube360.xex.", "Faltan fuentes árabes. Copia noto-sans-arabic-300/400/600/700.ttf junto a default.xex y tube360.xex.") \
  X(Arabic, "العربية", "العربية", "العربية") \
  X(Portuguese, "Português", "Português", "Português") \
  X(SignedOut, "Signed out", "Desconectado", "Sesión cerrada") \
  X(SignInAction, "Press A to sign in", "A para conectar", "Presiona A para iniciar sesión") \
  X(SignOutAction, "Press A to sign out", "A para desconectar", "Presiona A para cerrar sesión") \
  X(SignedInAs, "Connected as ", "Conectado como ", "Conectado como ") \
  X(Pending, "Waiting for sign-in", "Aguardando login", "Esperando el inicio de sesión") \
  X(SavedAutomatically, "Changes are saved automatically.", "As mudanças são salvas automaticamente.", "Los cambios se guardan automáticamente.") \
  X(SaveFailed, "Could not save settings on this device", "Não deu para salvar as configurações neste dispositivo", "No se pudo guardar la configuración en este dispositivo") \
  X(NoSpeaker, "No speaker configured on the server", "Nenhuma caixa configurada no servidor", "No hay un altavoz configurado en el servidor") \
  X(SpeakerChecking, "Checking for a speaker…", "Procurando uma caixa…", "Buscando un altavoz…") \
  X(SyncHelp, "Adjust in steps of 40 ms. Positive values advance the picture.", "Ajuste em passos de 40 ms. Valores positivos adiantam a imagem.", "Ajusta en pasos de 40 ms. Los valores positivos adelantan la imagen.") \
  X(ChooseRow, "Select", "Escolher", "Seleccionar") \
  X(ChangeValue, "Change", "Alterar", "Cambiar") \
  X(Open, "Open", "Abrir", "Abrir") \
  X(Three, "3", "3", "3") \
  X(Four, "4", "4", "4") \
  X(SignInTitle, "Sign in to YouTube", "Conectar ao YouTube", "Iniciar sesión en YouTube") \
  X(SignInStep, "On your phone or computer, open", "No celular ou computador, abra", "En tu teléfono o computadora, abre") \
  X(DeviceURL, "google.com/device", "google.com/device", "google.com/device") \
  X(EnterCode, "Enter this code", "Digite este código", "Ingresa este código") \
  X(LoginWaiting, "Waiting for Google confirmation…", "Aguardando a confirmação do Google…", "Esperando la confirmación de Google…") \
  X(LoginExpired, "Code expired. Press A to get a new one.", "O código expirou. Aperte A para gerar outro.", "El código venció. Presiona A para obtener otro.") \
  X(AccountError, "Could not update your account. Press A to retry.", "Não deu para atualizar a conta. Aperte A para tentar de novo.", "No se pudo actualizar la cuenta. Presiona A para reintentar.") \
  X(AccountUnavailable, "Account unavailable. Press A to retry.", "Conta indisponível. Aperte A para tentar de novo.", "Cuenta no disponible. Presiona A para reintentar.") \
  X(SignOutTitle, "Sign out of YouTube?", "Desconectar do YouTube?", "¿Cerrar sesión en YouTube?") \
  X(SignOutBody, "Your personal lists will be removed from this console.", "Suas listas pessoais deixarão de aparecer neste console.", "Tus listas personales dejarán de aparecer en esta consola.") \
  X(Confirm, "Confirm", "Confirmar", "Confirmar") \
  X(Cancel, "Cancel", "Cancelar", "Cancelar") \
  X(SignedOutToast, "Signed out of YouTube", "Conta do YouTube desconectada", "Se cerró la sesión de YouTube") \
  X(Library, "Library", "Biblioteca", "Biblioteca") \
  X(Channel, "Channel", "Canal", "Canal") \
  X(GoChannel, "Go to channel", "Ir para o canal", "Ir al canal") \
  X(Subscribe, "Subscribe", "Inscrever-se", "Suscribirse") \
  X(Subscribed, "Subscribed", "Inscrito", "Suscrito") \
  X(Videos, "Videos", "Vídeos", "Videos") \
  X(Playlists, "Playlists", "Playlists", "Listas de reproducción") \
  X(PlayAll, "Play all", "Tocar tudo", "Reproducir todo") \
  X(QuickPicks, "Quick picks", "Escolhas rápidas", "Selecciones rápidas") \
  X(Charts, "Charts", "Paradas", "Listas de éxitos") \
  X(Mixes, "Mixes", "Mixes", "Mixes") \
  X(Songs, "Songs", "Músicas", "Canciones") \
  X(Albums, "Albums", "Álbuns", "Álbumes") \
  X(Artists, "Artists", "Artistas", "Artistas") \
  X(Artist, "Artist", "Artista", "Artista") \
  X(Album, "Album", "Álbum", "Álbum") \
  X(Radio, "Start radio", "Iniciar rádio", "Iniciar radio") \
  X(NowPlaying, "Now playing", "Tocando agora", "Reproduciendo ahora") \
  X(Queue, "Up next", "A seguir", "A continuación") \
  X(NextTrack, "Next track", "Próxima faixa", "Siguiente pista") \
  X(Lyrics, "Lyrics", "Letra", "Letra") \
  X(NoLyrics, "Lyrics are not available for this track.", "Letra indisponível para esta faixa.", "La letra de esta canción no está disponible.") \
  X(CardMenu, "Video options", "Opções do vídeo", "Opciones del video") \
  X(AddedLater, "Saved to Watch later", "Salvo em Assistir mais tarde", "Guardado en Ver más tarde") \
  X(AccountRequired, "Sign in to YouTube in Settings to save and subscribe.", "Conecte sua conta em Configurações para salvar e se inscrever.", "Inicia sesión en Configuración para guardar y suscribirte.") \
  X(ActionFailed, "Could not save. Press A to retry.", "Não foi possível salvar. Aperte A para tentar de novo.", "No se pudo guardar. Presiona A para reintentar.") \
  X(Saving, "Saving…", "Salvando…", "Guardando…") \
  X(StartingQueue, "Preparing the queue…", "Preparando a fila…", "Preparando la cola…") \
  X(History, "History", "Histórico", "Historial") \
  X(MusicSearch, "Search songs, albums and artists", "Buscar músicas, álbuns e artistas", "Buscar canciones, álbumes y artistas") \
  X(NoLive, "No live streams on this channel.", "Este canal não tem transmissões ao vivo.", "Este canal no tiene transmisiones en vivo.") \
  X(MoreLoading, "Loading the next page…", "Carregando a próxima página…", "Cargando la siguiente página…") \
  X(Results, "Search results", "Resultados da busca", "Resultados de búsqueda") \
  X(AllResults, "All results", "Tudo", "Todos") \
  X(MoreOptions, "Save / options", "Salvar / opções", "Guardar / opciones") \
  X(ChooseTab, "Choose a tab", "Escolher aba", "Elegir pestaña") \
  X(MusicIntro, "Your next favourite sound.", "Seu próximo som favorito.", "Tu próxima canción favorita.") \
  X(YourMusic, "Music from your account", "Música da sua conta", "Música de tu cuenta") \
  X(PublicMusic, "Public Music", "Música pública", "Música pública") \
  X(MusicRecent, "Recently played", "Ouvidas recentemente", "Escuchadas recientemente") \
  X(MusicLiked, "Liked music", "Músicas curtidas", "Música que te gusta") \
  X(MusicAccountUnavailable, "Account Music is unavailable. Try again later.", "A música da conta está indisponível. Tente mais tarde.", "La música de la cuenta no está disponible. Inténtalo más tarde.") \
  X(QueueLimit, "Playlists play up to 256 tracks.", "As playlists tocam até 256 faixas.", "Las listas reproducen hasta 256 pistas.") \
  X(SubscribeConfirm, "Unsubscribe from this channel?", "Cancelar a inscrição neste canal?", "¿Cancelar la suscripción a este canal?") \
  X(GoLive, "Jump to live", "Ir para o ao vivo", "Ir al directo") \
  X(ChannelLive, "Live", "Ao vivo", "En vivo") \
  X(MusicSpeakerFallback, "Speaker did not respond: sound on the TV.", "A caixa não respondeu: som na TV.", "El altavoz no responde: sonido en la TV.") \
  X(BtnA, "A", "A", "A") X(BtnB, "B", "B", "B") X(BtnX, "X", "X", "X") X(BtnY, "Y", "Y", "Y") \
  X(BtnView, "View", "View", "View") X(BtnMenu, "Menu", "Menu", "Menu") \
  X(BtnShoulders, "LB RB", "LB RB", "LB RB") X(BtnTriggers, "LT RT", "LT RT", "LT RT") \
  X(BtnHorizontal, "< >", "< >", "< >") X(BtnVertical, "↑ ↓", "↑ ↓", "↑ ↓") \
  X(SearchSuggestions, "Suggestions", "Sugestões", "Sugerencias") \
  X(SearchRecentTag, "Recent", "Recente", "Reciente") \
  X(SearchFilters, "Filters", "Filtros", "Filtros") \
  X(SearchType, "Result type", "Tipo de resultado", "Tipo de resultado") \
  X(SearchDuration, "Duration", "Duração", "Duración") \
  X(SearchDate, "Upload date", "Data de envio", "Fecha de subida") \
  X(SearchOrder, "Sort by", "Ordenar por", "Ordenar por") \
  X(SearchShort, "Short · under 3 min", "Curta · menos de 3 min", "Corta · menos de 3 min") \
  X(SearchMedium, "Medium · 3–20 min", "Média · 3–20 min", "Media · 3–20 min") \
  X(SearchLong, "Long · over 20 min", "Longa · mais de 20 min", "Larga · más de 20 min") \
  X(SearchShortLabel, "Short", "Curta", "Corta") \
  X(SearchMediumLabel, "Medium", "Média", "Media") \
  X(SearchLongLabel, "Long", "Longa", "Larga") \
  X(SearchToday, "Today", "Hoje", "Hoy") \
  X(SearchWeek, "This week", "Esta semana", "Esta semana") \
  X(SearchMonth, "This month", "Este mês", "Este mes") \
  X(SearchYear, "This year", "Este ano", "Este año") \
  X(SearchRelevance, "Relevance", "Relevância", "Relevancia") \
  X(SearchViews, "View count", "Visualizações", "Visualizaciones") \
  X(SearchApply, "Apply", "Aplicar", "Aplicar") \
  X(SearchClear, "Clear filters", "Limpar filtros", "Borrar filtros") \
  X(SearchAny, "Any", "Qualquer", "Cualquiera") \
  X(OnConsole, "On this console", "Neste console", "En esta consola") \
  X(LocalLater, "Watch later · console", "Assistir mais tarde · console", "Ver más tarde · consola") \
  X(LocalHistory, "Continue · console", "Continuar · console", "Continuar · consola") \
  X(LocalOnly, "Saved here. Never sent to your account.", "Salvo aqui. Nunca enviado à sua conta.", "Guardado aquí. Nunca se envía a tu cuenta.") \
  X(RemoveLocalLater, "Remove from local list", "Remover da lista local", "Quitar de la lista local") \
  X(ClearLocalHistory, "Clear local history", "Apagar histórico local", "Borrar historial local") \
  X(ClearLocalLater, "Clear local watch later", "Apagar lista local", "Borrar lista local") \
  X(ConfirmLocalClear, "Delete these saved items on this console?", "Apagar estes itens salvos neste console?", "¿Borrar estos elementos guardados en esta consola?") \
  X(LocalSaving, "Saving on this console…", "Salvando neste console…", "Guardando en esta consola…") \
  X(LocalSaved, "Saved on this console", "Salvo neste console", "Guardado en esta consola") \
  X(LocalSaveFailed, "Could not save locally. Check storage space.", "Não foi possível salvar. Confira o espaço no disco.", "No se pudo guardar. Revisa el espacio en el disco.") \
  X(LocalCorrupt, "Local file unreadable. Lists start empty.", "Arquivo local ilegível. Listas iniciadas vazias.", "Archivo local ilegible. Las listas empiezan vacías.") \
  X(ResumeLocal, "Continue where you left off?", "Continuar de onde parou?", "¿Continuar donde lo dejaste?") \
  X(StartBeginning, "Start from beginning", "Começar do início", "Empezar desde el principio") \
  X(LocalLimit, "Up to 200 videos · up to 500 positions", "Até 200 vídeos · até 500 posições", "Hasta 200 videos · hasta 500 posiciones") \
  X(ChannelShorts, "Shorts", "Shorts", "Shorts") \
  X(LocalEmpty, "Empty", "Vazio", "Vacío") \
  X(LocalVideo, "video", "vídeo", "video") \
  X(LocalVideos, "videos", "vídeos", "videos") \
  X(LocalPosition, "position", "posição", "posición") \
  X(LocalPositions, "positions", "posições", "posiciones") \
  X(MembersXboxUnavailable, "Members-only videos can't play on Xbox yet.", "Vídeos de membros ainda não tocam no Xbox.", "Los videos para miembros aún no se reproducen en Xbox.") \
  X(SettingShowShorts, "Show Shorts", "Mostrar Shorts", "Mostrar Shorts") \
  X(SettingShortsRow, "In a row", "Em fileira", "En una fila") \
  X(SettingShortsHide, "Hide", "Esconder", "Ocultar") \
  X(SettingErrorReports, "Error reports", "Relatórios de erro", "Informes de errores") \
  X(SettingOn, "On", "Ligado", "Activado") \
  X(SettingOff, "Off", "Desligado", "Desactivado") \
  /* ---- F61 Início e coleção */ \
  X(MembersOnly, "Members only", "Só para membros", "Solo para miembros") \
  /* */ \
  /* ---- F62 Busca e canal */ \
  X(ChannelHome, "Home", "Início", "Inicio") \
  X(SortRecent, "Latest", "Mais recentes", "Más recientes") \
  X(SortPopular, "Trending", "Em alta", "En tendencia") \
  X(SortOldest, "Oldest", "Mais antigos", "Más antiguos") \
  X(SearchTypeChannels, "Channels", "Canais", "Canales") \
  /* */ \
  /* ---- F63 Player e Shorts */ \
  X(PlayerShelfNext, "Up next", "A seguir", "A continuación") \
  X(PlayerShelfChannel, "From the channel", "Do canal", "Del canal") \
  X(PlayerShelfRelated, "Related", "Relacionados", "Relacionados") \
  X(PlayerNoNext, "No next video", "Não há próximo vídeo", "No hay siguiente vídeo") \
  X(PlayerNoPrev, "No previous video", "Não há vídeo anterior", "No hay vídeo anterior") \
  X(PlayerLaterAdded, "Saved to Watch later", "Salvo em Assistir mais tarde", "Guardado en Ver más tarde") \
  X(PlayerLaterRemoved, "Removed from Watch later", "Removido de Assistir mais tarde", "Quitado de Ver más tarde") \
  X(ShortsStep, "Previous / next", "Anterior / próximo", "Anterior / siguiente") \
  /* */ \
  /* ---- F64 Configurações e controle */ \
  X(CtlPlayPause, "Play / pause", "Tocar / pausar", "Reproducir / pausar") \
  X(CtlAddQueue, "Add to queue", "Adicionar à fila", "Añadir a la cola") \
  X(CtlSongVideo, "Song / Video", "Música / Vídeo", "Canción / Video") \
  X(CtlSwitchTab, "Switch tab", "Trocar de aba", "Cambiar pestaña") \
  X(CtlPrevNext, "Previous / next", "Anterior / próximo", "Anterior / siguiente") \
  X(CtlPage, "Jump a page", "Pular uma página", "Saltar una página") \
  X(CtlSeek, "Seek (hold)", "Avançar (segurar)", "Avanzar (mantener)") \
  X(CtlSection, "Section", "Seção", "Sección") \
  X(SecAccount, "Account", "Conta", "Cuenta") \
  X(SecVideo, "Video", "Vídeo", "Video") \
  X(SecSound, "Sound", "Som", "Sonido") \
  X(SecScreen, "Screen and language", "Tela e idioma", "Pantalla e idioma") \
  X(SecTimer, "Timer", "Timer", "Temporizador") \
  X(SecPrivacy, "Privacy", "Privacidade", "Privacidad") \
  X(SecSystem, "System", "Sistema", "Sistema") \
  X(CtlVolume, "Volume", "Volume", "Volumen") \
  X(SettingLiveLowLatency, "Live: lowest delay", "Live com atraso mínimo", "Directo con el menor retraso") \
  X(LiveLowLatencyHelp, "Starts near live and catches up at 1.05x; TV only.", "Começa perto do ao vivo e alcança a 1,05x; só na TV.", "Empieza cerca del directo y alcanza a 1,05x; solo en la TV.") \
  X(PageMenu, "Page options", "Opções da página", "Opciones de la página") \
  X(CtlCheckAgain, "Check again", "Verificar de novo", "Comprobar de nuevo") \
  X(UpdateToastHead, "Update ", "Atualização ", "Actualización ") \
  X(UpdateToastTail, " available · Settings > System", " disponível · Configurações > Sistema", " disponible · Configuración > Sistema") \
  X(SettingVolume, "TV volume", "Volume na TV", "Volumen en la TV") \
  /* */ \
  /* ---- F65 Música */ \
  X(MusicSeekBar, "Seek", "Avançar ou voltar", "Adelantar o retroceder") \
  X(MusicExplore, "Explore", "Explorar", "Explorar") \
  X(MusicTopResult, "Top result", "Melhor resultado", "Mejor resultado") \
  X(MusicFeatured, "Featured playlists", "Playlists em destaque", "Listas destacadas") \
  X(MusicRelated, "Related", "Relacionados", "Relacionados") \
  X(MusicRelatedEmpty, "Nothing related to this track.", "Nada relacionado a esta faixa.", "Nada relacionado con esta pista.") \
  X(MusicAutoplay, "Autoplay", "Reprodução automática", "Reproducción automática") \
  X(MusicAutoplayOn, "Autoplay on", "Reprodução automática ligada", "Reproducción automática activada") \
  X(MusicAutoplayOff, "Autoplay off", "Reprodução automática desligada", "Reproducción automática desactivada") \
  X(MusicLyricsFailed, "Could not load the lyrics.", "Não foi possível carregar a letra.", "No se pudo cargar la letra.") \
  X(MusicAddedQueue, "Added to the queue", "Adicionado à fila", "Añadido a la cola") \
  X(MusicMixOf, "Mix of", "Mix de", "Mix de") \
  X(MusicLibrarySignIn, "Sign in with your account to see your Library.", "Entre com a sua conta para ver a sua Biblioteca.", "Inicia sesión con tu cuenta para ver tu Biblioteca.") \
  X(MusicSongTab, "Song", "Música", "Canción") \
  X(MusicVideoTab, "Video", "Vídeo", "Video") \
  X(MusicMoveTrack, "Move track", "Mover faixa", "Mover pista") \
  X(MusicMovingTrack, "Moving: up and down place it, A drops it", "Movendo: cima e baixo posicionam, A solta", "Moviendo: arriba y abajo la colocan, A la suelta") \
  X(MusicShuffleOn, "Shuffle on", "Aleatório ligado", "Aleatorio activado") \
  X(MusicShuffleOff, "Shuffle off", "Aleatório desligado", "Aleatorio desactivado") \
  X(MusicRepeatOff, "Repeat off", "Repetir desligado", "Repetir desactivado") \
  X(MusicRepeatAll, "Repeat the queue", "Repetir a fila", "Repetir la cola") \
  X(MusicRepeatOne, "Repeat this track", "Repetir esta faixa", "Repetir esta pista") \
  X(MusicQueueEmpty, "The queue is empty.", "A fila está vazia.", "La cola está vacía.") \
  X(MusicQueueServer, "The queue is controlled by the server; jump and reorder are not available.", "A fila é controlada pelo servidor; pular e reordenar não estão disponíveis.", "El servidor controla la cola; saltar y reordenar no están disponibles.") \
  X(MusicVideoLoading, "Loading the video…", "Carregando o vídeo…", "Cargando el vídeo…") \
  /* */ \
  /* ---- F66 controle pelo celular */ \
  X(SettingPhoneRemote, "Phone as remote", "Celular como controle", "Móvil como control") \
  X(PhoneRemoteHelp, "Control the app from a phone browser on the same Wi-Fi. Press A to see the address and the code.", "Controle o app pelo navegador do celular no mesmo Wi-Fi. Aperte A para ver o endereço e o código.", "Controla la app desde el navegador del móvil en el mismo Wi-Fi. Pulsa A para ver la dirección y el código.") \
  X(RemoteTitle, "Phone as remote", "Celular como controle", "Móvil como control") \
  X(RemoteSteps, "On your phone, connected to the same Wi-Fi, open this address in the browser and type the code.", "No celular, ligado no mesmo Wi-Fi, abra este endereço no navegador e digite o código.", "En el móvil, conectado al mismo Wi-Fi, abre esta dirección en el navegador y escribe el código.") \
  X(RemoteCode, "Pairing code", "Código de pareamento", "Código de emparejamiento") \
  X(RemoteSafe, "Only devices on your home network can connect. The code changes after each pairing.", "Só aparelhos da sua rede de casa conseguem conectar. O código muda a cada pareamento.", "Solo los dispositivos de tu red doméstica pueden conectarse. El código cambia en cada emparejamiento.") \
  X(RemoteNoNetwork, "No network. Connect the console to the Wi-Fi or cable.", "Sem rede. Conecte o console ao Wi-Fi ou ao cabo.", "Sin red. Conecta la consola al Wi-Fi o al cable.") \
  X(RemoteWrongCode, "Someone typed the wrong code. Read the new code above.", "Alguém errou o código. Leia o código novo acima.", "Alguien se equivocó con el código. Lee el código nuevo de arriba.") \
  X(RemoteTooMany, "Too many wrong codes. Open it again to generate another code.", "Muitas tentativas erradas. Abra de novo para gerar outro código.", "Demasiados intentos incorrectos. Ábrelo de nuevo para generar otro código.") \
  X(RemoteConnected, "Phone connected", "Celular conectado", "Móvil conectado") \
  /* */ \
  /* ---- F67 membros tocando */ \
  X(LiveWaiting, "The live has not started yet or is paused", "A live ainda não começou ou está parada", "El directo aún no empezó o está detenido") \
  X(LiveOnTv, "Live plays on the TV: the speaker does not receive live streams", "Live toca na TV: a caixa ainda não recebe transmissão ao vivo", "El directo suena en la TV: el altavoz aún no recibe transmisiones en vivo") \
  /* */ \
  /* ---- fim da lista */

enum class Key {
#define TEXT_KEY(k, en, pt, es) k,
  TUBE360_TEXT(TEXT_KEY)
#undef TEXT_KEY
  Count
};
static_assert(size_t(Key::None)==0);
inline constexpr const char* texts[][3] = {
#define TEXT_VALUE(k, en, pt, es) {en, pt, es},
  TUBE360_TEXT(TEXT_VALUE)
#undef TEXT_VALUE
};
#undef TUBE360_TEXT
static_assert(sizeof(texts) / sizeof(texts[0]) == (size_t)Key::Count);
#include "i18n_ar.hpp"
inline bool rtl(Lang lang) { return lang == Lang::AR; }
inline const char* get(Lang lang, Key key) {
  if (lang == Lang::AR) { const char* ar = arabic(key); if (ar) return ar; }
  return texts[(size_t)key][lang == Lang::PT ? 1 : lang == Lang::ES ? 2 : 0]; }
inline std::string localCount(Lang lang, size_t count, bool history) {
  if (!count) return get(lang,Key::LocalEmpty);
  Key unit=history ? (count==1 ? Key::LocalPosition : Key::LocalPositions) : (count==1 ? Key::LocalVideo : Key::LocalVideos);
  if (lang==Lang::AR) return std::string(get(lang,history ? Key::LocalPositions : Key::LocalVideos))+": "+std::to_string(count);
  return std::to_string(count)+" "+get(lang,unit);
}
inline Key quality(int q) { return q == 360 ? Key::Q360 : q == 480 ? Key::Q480 : Key::Q720; }

// player.hpp mantém os diagnósticos internos; só a apresentação é traduzida.
inline Key playerError(const std::string& error) {
  if (error == "members_unsupported") return Key::MembersXboxUnavailable;
  if (error == "live_waiting") return Key::LiveWaiting;
  if (error == "manual_quality_unavailable") return Key::ManualQualityMissing;
  if (error == "manual_quality_failed") return Key::ManualQualityFailed;
  if (error == "console") return Key::ConsoleVideoError;
  if (error == "Vídeo só para membros do canal") return Key::VideoMembers;
  if (error == "Vídeo privado") return Key::VideoPrivate;
  if (error == "Vídeo com restrição de idade") return Key::VideoAge;
  if (error == "Vídeo indisponível no YouTube") return Key::VideoGone;
  if (error.find("Nest") != std::string::npos) return Key::SpeakerError;
  return Key::VideoOpenError;
}
}  // namespace i18n
