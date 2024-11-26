#include "moviewidget.h"
#include <qboxlayout.h>
#include <QPainter>

MovieWidget::MovieWidget(const Movie& _movie, JellyfinApi *_api, QWidget *parent)
    : MediaWidget{parent}, movie(_movie), jellyfinApi(_api)
{
    this->setMinimumSize(600, 400);

    // Banner image with gradient
    bannerLabel = new QLabel(this);
    bannerLabel->setFixedHeight(200);
    bannerLabel->setStyleSheet("border: none; background-color: black;"); // Default banner

    // Primary artwork
    primaryLabel = new QLabel(this);
    primaryLabel->setFixedSize(120, 180);
    primaryLabel->setStyleSheet("border: 1px solid #ccc;");
    primaryLabel->setAlignment(Qt::AlignCenter);

    // Title and description
    titleLabel = new QLabel(movie.getName(), this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; margin: 5px 0;");
    descriptionLabel = new QLabel(movie.getOverview(), this);
    descriptionLabel->setWordWrap(true);

    // Action buttons
    backButton = new QPushButton("Back", this);
    playButton = new QPushButton("Play", this);
    markPlayedButton = new QPushButton("Mark as Played", this);
    favoriteButton = new QPushButton("Favorite", this);

    connect(backButton, &QPushButton::clicked, this, [this](){
        emit done();
    });
    connect(playButton, &QPushButton::clicked, this, &MovieWidget::onPlayClicked);
    connect(markPlayedButton, &QPushButton::clicked, this, &MovieWidget::onMarkPlayedClicked);
    connect(favoriteButton, &QPushButton::clicked, this, &MovieWidget::onFavoriteClicked);

    // Layout setup
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Banner section
    QVBoxLayout *bannerLayout = new QVBoxLayout();
    bannerLayout->setContentsMargins(0, 0, 0, 0);
    bannerLayout->addWidget(bannerLabel);
    mainLayout->addLayout(bannerLayout);

    // Movie info section
    QHBoxLayout *infoLayout = new QHBoxLayout();
    infoLayout->addWidget(primaryLabel);
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(descriptionLabel);
    infoLayout->addLayout(textLayout);
    mainLayout->addLayout(infoLayout);

    // Action buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(backButton);
    buttonLayout->addWidget(playButton);
    buttonLayout->addWidget(markPlayedButton);
    buttonLayout->addWidget(favoriteButton);
    mainLayout->addLayout(buttonLayout);

    fetchImages();
}

void MovieWidget::fetchImages()
{
    // Fetch primary image
    jellyfinApi->getItemImage(movie.getId(), "Primary", 10, this, [this](bool success, QPixmap image, const QString &errorMessage) {
        if (success) {
            setPrimaryImage(image);
        } else {
            qWarning() << "Failed to fetch primary image:" << errorMessage;
        }
    });

    // Fetch banner image
    jellyfinApi->getItemImage(movie.getId(), "Backdrop", 10, this, [this](bool success, QPixmap image, const QString &errorMessage) {
        if (success) {
            setBannerImage(image);
        } else {
            qWarning() << "Failed to fetch banner image:" << errorMessage;
        }
    });
}

void MovieWidget::setPrimaryImage(const QPixmap &image)
{
    primaryLabel->setPixmap(image.scaled(primaryLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MovieWidget::setBannerImage(const QPixmap &image)
{
    QPixmap gradientOverlay(image.size());
    gradientOverlay.fill(Qt::transparent);

    QPainter painter(&gradientOverlay);
    QLinearGradient gradient(0, 0, 0, image.height());
    gradient.setColorAt(0.0, QColor(0, 0, 0, 200)); // Dark transparent color
    gradient.setColorAt(1.0, Qt::transparent);

    painter.fillRect(gradientOverlay.rect(), gradient);
    painter.drawPixmap(0, 0, image);

    bannerLabel->setPixmap(gradientOverlay.scaled(bannerLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
}

void MovieWidget::onPlayClicked()
{
    emit playRequested(movie.getId());
}

void MovieWidget::onMarkPlayedClicked()
{
    emit markPlayedRequested(movie.getId());
}

void MovieWidget::onFavoriteClicked()
{
    isFavorite = !isFavorite;
    favoriteButton->setText(isFavorite ? "Unfavorite" : "Favorite");
    emit favoriteToggled(movie.getId(), isFavorite);
}
