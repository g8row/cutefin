    #include "homewindow.h"
    #include "setupwindow.h"
    #include "moviecard.h"

    #include <QSettings>
    #include <QScrollArea>
    #include <QHBoxLayout>
    #include <QVBoxLayout>
    #include <QLabel>

    HomeWindow::HomeWindow(QWidget *parent)
        : QMainWindow{parent}
    {
        QSettings *settings = new QSettings("g8row", "cutefin");
        SetupWindow *setupWindow = nullptr;
        JellyfinApi* jellyfinApi = new JellyfinApi(this);

        this->setMinimumSize(600,500);

        this->show();
        if(!settings->contains("server_name") or !settings->contains("access_token")){
            setupWindow = new SetupWindow(settings, jellyfinApi, nullptr);
            connect(setupWindow, &SetupWindow::setupComplete, this, [this](){
                this->show();
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

        // Main layout for the window
        QScrollArea *mainArea = new QScrollArea(this);
        mainArea->setWidgetResizable(true);
        QWidget *mainWidget = new QWidget();
        QVBoxLayout *mainLayout = new QVBoxLayout(mainWidget);
        mainLayout->addWidget(new QLabel("Latest Movies", this));
        mainLayout->addWidget(moviesArea);
        mainLayout->addWidget(new QLabel("Latest Movies", this));
        mainLayout->addWidget(new QLabel("Latest Movies", this));
        mainLayout->addWidget(new QLabel("Latest Movies", this));
        mainLayout->addWidget(new QLabel("Latest Movies", this));
        mainLayout->addWidget(new QLabel("Latest Movies", this));
        mainLayout->addWidget(new QLabel("Latest Movies", this));
        mainLayout->addWidget(new QLabel("Latest Movies", this));
        mainLayout->addWidget(new QLabel("Latest Movies", this));
        mainLayout->addWidget(new QLabel("Latest Movies", this));
        mainArea->setWidget(mainWidget);
        this->setCentralWidget(mainArea);

        connect(jellyfinApi, &JellyfinApi::latestMoviesResponse, this, [this, moviesWidget, jellyfinApi](bool success, QList<Movie> movies, const QString &errorMessage = QString()){
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
