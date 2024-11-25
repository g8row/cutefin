    #include "homewindow.h"
    #include "setupwindow.h"
    #include "moviecard.h"

    #include <QScrollArea>
    #include <QHBoxLayout>
    #include <QVBoxLayout>
    #include <QLabel>

    HomeWindow::HomeWindow(QWidget *parent)
        : QMainWindow{parent}
    {
        settings = new QSettings("g8row", "cutefin");
        SetupWindow *setupWindow = nullptr;
        jellyfinApi = new JellyfinApi(this);

        this->setMinimumSize(600,500);

        this->show();
        if(!settings->contains("server_name") or !settings->contains("access_token")){
            setupWindow = new SetupWindow(settings, jellyfinApi, nullptr);
            connect(setupWindow, &SetupWindow::setupComplete, this, [this, setupWindow](){
                this->show();
                delete setupWindow;
            });
            this->hide();
        } else {
            jellyfinApi->setUrl(settings->value("server_name").toString());
            jellyfinApi->setAccessToken(settings->value("access_token").toString());
        }

        // Scrollable area for movies
        QScrollArea *moviesArea = new QScrollArea();
        moviesArea->setWidgetResizable(true);
        QWidget *moviesWidget = new QWidget();
        QHBoxLayout *moviesLayout = new QHBoxLayout(moviesWidget);
        moviesWidget->setLayout(moviesLayout);
        moviesWidget->setMinimumHeight(300);
        moviesArea->setWidget(moviesWidget);
        moviesArea->setMinimumHeight(330);
        moviesArea->setMaximumHeight(330);


        // Main layout for the window
        QScrollArea *mainArea = new QScrollArea(this);
        mainArea->setWidgetResizable(true);
        QWidget *mainWidget = new QWidget();
        QVBoxLayout *mainLayout = new QVBoxLayout(mainWidget);
        mainLayout->addWidget(new QLabel("Latest Movies", this));
        mainLayout->addWidget(moviesArea);
        mainArea->setWidget(mainWidget);
        this->setCentralWidget(mainArea);

        connect(jellyfinApi, &JellyfinApi::latestMoviesResponse, this, [this, moviesWidget](bool success, QList<Movie> movies, const QString &errorMessage = QString()){
            if (success) {
                for (const Movie &movie : movies) {
                    MovieCard *movieCard = new MovieCard(movie, moviesWidget);
                    movieCard->fetchPoster(jellyfinApi);
                    moviesWidget->layout()->addWidget(movieCard);
                }
            } else {
                qWarning() << "Failed to fetch movies:" << errorMessage;
            }
        });

        jellyfinApi->getLatestMovies();
    }

    HomeWindow::~HomeWindow(){
        delete jellyfinApi;
        delete settings;
    }
