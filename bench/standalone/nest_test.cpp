#include "nest.hpp"
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc != 2 || !SDL_Init(SDL_INIT_EVENTS)) return 1;
    nest::Speaker speaker;
    speaker.init(std::string(argv[1])+"/");
    std::string line;
    while (std::getline(std::cin,line)) {
        std::istringstream input(line);
        std::string command; input >> command;
        if (command == "play") {
            std::string url; double position = 0; int autoplay = 1;
            input >> url >> position >> autoplay;
            speaker.play(url,"fixture",position,300,autoplay);
        } else if (command == "pause") speaker.pause(true);
        else if (command == "resume") speaker.pause(false);
        else if (command == "stop") speaker.release();
        else if (command == "volume") { double level = 0; int muted = 0; input >> level >> muted; speaker.volume(level,muted != 0); }
        else if (command == "fade") { double gain = 1; input >> gain; speaker.fade(gain); }
        else if (command == "quit") break;
        else if (command != "sample") return 2;
        auto status = speaker.sample();
        nest::Object out{{"state",nest::J(double(status.state))},{"generation",nest::J(double(status.generation))},
                         {"position",nest::J(status.position)},{"updated",nest::J(status.updated)},
                         {"observed",nest::J(status.observed)},{"now",nest::J(SDL_GetTicksNS()/1e6)},
                         {"quiet",nest::J(status.quiet)},{"connected",nest::J(status.connected)},
                         {"released",nest::J(status.released)}};
        std::cout << nest::J(out).serialize() << std::endl;
    }
    speaker.shutdown();
    SDL_Quit();
}
