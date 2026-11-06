# Synesthesia Examples

A collection of examples, experiments, and implementations using [Synesthesia](https://github.com/FrankSteps/synesthesia).

This repository explores different ways to integrate Synesthesia into external projects, applications, and hardware, bringing music-driven colors to a variety of creative applications.

## Examples

This repository contains independent projects demonstrating different ways to use and integrate Synesthesia.

Each example has its own implementation, requirements, and instructions.

| Example                                | Description                                                                                                | Platform     |
|---                                     |---                                                                                                         |---           |
| [Akko Keyboard Driver](./akko-driver/) | Synchronizes the Akko MG75S keyboard's lighting with Synesthesia colors and real-time audio visualization. | C++ / Linux  |
| [ESP32 project - Coming soon]          | Synchronizes the RGB LED strip's lighting with Synesthesia colors and real-time                            | ESP32        |

More examples and integrations will be added over time.

## Project Structure

```text
synesthesia-examples/
├── akko-driver
│   ├── builds
│   │   ├── akko_bridge
│   │   └── akko_music
│   └── src
│       ├── akko_bridge.cpp
│       └── akko_music.cpp
├── LICENSE
├── makefile
└── README.md

```

Each directory represents an independent example or experiment. Its internal structure depends on the technologies and requirements of that particular implementation.

## Getting Started

1. Make sure you have [Synesthesia](https://github.com/FrankSteps/synesthesia) installed and running, if required by the example.
2. Browse the available examples.
3. Open the corresponding directory and follow its README for dependencies, configuration, compilation, and usage instructions.

Requirements and setup procedures may vary between examples.

## Contributing

This repository primarily serves as a collection of my personal experiments and implementations using Synesthesia. Feel free to explore the code and adapt the examples to your own projects.

## License

This repository is licensed under the [MIT License](LICENSE).
