#include "moviecard.h"


MovieCard::MovieCard(const Movie &movie, QWidget *parent)
    : QWidget(parent), movie(movie)
{
    layout = new QVBoxLayout;
    this->setLayout(layout);
    this->setFixedWidth(175); // Set consistent card width

    layout->setAlignment(Qt::AlignHCenter);

    titleLabel = new QLabel(movie.getName());
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setWordWrap(true); // Handle long titles

    posterLabel = new QLabel();
    posterLabel->setFixedSize(150, 225); // Adjust poster size
    posterLabel->setAlignment(Qt::AlignCenter);
    posterLabel->setStyleSheet("border: 1px solid black; background-color: #eaeaea;"); // Placeholder styling

    layout->addWidget(posterLabel);
    layout->addWidget(titleLabel);

    this->setStyleSheet("border: 1px solid transparent; border-radius: 5px;");
}

void MovieCard::fetchPoster(JellyfinApi *jellyfinApi) {
    jellyfinApi->getItemImage(movie.getId(), "Primary", 10, this,
                              [this](bool success, const QPixmap image, const QString &errorMessage) {
                                  if (success) {
                                      posterLabel->setPixmap(image.scaled(posterLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                                  } else {
                                      posterLabel->setText("Image unavailable");
                                      qWarning() << "Failed to fetch image for" << movie.getName() << ":" << errorMessage;
                                  }
                              });
}

void MovieCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(movie); // Emit the clicked signal
    }
    QWidget::mousePressEvent(event);
}

void MovieCard::enterEvent(QEnterEvent *event)
{
    // Highlight the card visually on hover
    this->setStyleSheet("border: 1px solid #0078D7; background-color: rgba(0, 120, 215, 0.1); border-radius: 5px;");
    QWidget::enterEvent(event);
}

void MovieCard::leaveEvent(QEvent *event)
{
    // Revert the hover effect
    this->setStyleSheet("border: 1px solid transparent; background-color: transparent; border-radius: 5px;");
    QWidget::leaveEvent(event);
}

MovieCard::~MovieCard(){
    delete posterLabel;
    delete titleLabel;
    delete layout;
}
