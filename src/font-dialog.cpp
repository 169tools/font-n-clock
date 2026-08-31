/*
Font-n-Clock
Copyright (C) 2026 mizznoff <mizznoff@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "font-dialog.hpp"

#include <obs-frontend-api.h>

#include "obs-module.h"
#include "text-renderer.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QLatin1Char>
#include <QListWidget>
#include <QObject>
#include <QOverload>
#include <QPixmap>
#include <QStringLiteral>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <QtCore/qcontainerfwd.h>

#include <memory>
#include <string>

constexpr double preview_size = 30;

QPixmap render_preview(const QString &family, const QString &style, const date_format format, const bool twelve_hour)
{
	clock_style spec{.format = format, .twelve_hour = twelve_hour};
	spec.font_face = family.toStdString();
	spec.font_style = style.toStdString();
	spec.size = preview_size;
	spec.color = 0xffffffff;

	spec.colon_offset_ratio = suggest_colon_offset_ratio(spec);

	const std::unique_ptr<prepared_clock> clock = prepare_clock(spec);
	if (!clock) {
		return {};
	}

	const rendered_text bitmap = clock->render({
		.date = format_date(format, sample_month, sample_day, sample_weekday),
		.time = format_time(sample_hour, sample_minute, twelve_hour),
		.meridiem = format_meridiem(sample_hour, twelve_hour),
	});
	if (!bitmap.valid()) {
		return {};
	}

	const QImage image(bitmap.pixels.data(), static_cast<int>(bitmap.width), static_cast<int>(bitmap.height),
			   static_cast<int>(bitmap.width) * 4, QImage::Format_RGBA8888_Premultiplied);

	return QPixmap::fromImage(image);
}

QString pick_default_style(const QStringList &styles)
{
	const QString preferred = QStringLiteral("Bold");
	if (styles.contains(preferred)) {
		return preferred;
	}
	return styles.isEmpty() ? QString() : styles.first();
}

bool select_font(std::string &face, std::string &style, const date_format format, const bool twelve_hour)
{
	auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	QDialog dialog(parent);
	dialog.setWindowTitle(QString::fromUtf8((obs_module_text("ClockSource.Font"))));
	dialog.resize(480, 560);

	auto *layout = new QVBoxLayout(&dialog);

	layout->addWidget(new QLabel(QString::fromUtf8(obs_module_text("ClockSource.Font")), &dialog));

	auto *families = new QListWidget(&dialog);
	for (const std::string &family : available_font_families()) {
		families->addItem(QString::fromStdString(family));
	}
	families->sortItems();
	layout->addWidget(families, 1);

	auto *lower = new QGridLayout;
	layout->addLayout(lower);

	lower->addWidget(new QLabel(QString::fromUtf8(obs_module_text("ClockSource.FontStyle")), &dialog), 0, 0);
	lower->addWidget(new QLabel(QString::fromUtf8(obs_module_text("ClockSource.Preview")), &dialog), 0, 1);

	auto *styles = new QListWidget(&dialog);
	lower->addWidget(styles, 1, 0);

	auto *preview = new QLabel(&dialog);
	preview->setAlignment(Qt::AlignCenter);
	preview->setFrameStyle(QFrame::Sunken | QFrame::Panel);
	lower->addWidget(preview, 1, 1);

	lower->setColumnStretch(0, 1);
	lower->setColumnStretch(1, 1);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);

	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	const QString wanted_style = QString::fromStdString(style);
	const int visible_style_rows = 4;

	QObject::connect(families, &QListWidget::currentTextChanged, styles,
			 [styles, wanted_style, visible_style_rows, first_fill = true](const QString &family) mutable {
				 QStringList available;
				 for (const std::string &name : available_font_styles(family.toStdString())) {
					 available.append(QString::fromStdString(name));
				 }
				 styles->clear();
				 styles->addItems(available);

				 if (styles->count() > 0) {
					 styles->setFixedHeight(styles->sizeHintForRow(0) * visible_style_rows +
								styles->frameWidth() * 2);
				 }

				 const QString selected = first_fill && available.contains(wanted_style)
								  ? wanted_style
								  : pick_default_style(available);
				 first_fill = false;

				 const auto matches = styles->findItems(selected, Qt::MatchExactly);
				 if (!matches.isEmpty()) {
					 styles->setCurrentItem(matches.first());
				 }
			 });

	const auto refresh_preview = [families, styles, preview, format, twelve_hour]() {
		if (!families->currentItem()) {
			return;
		}

		const QString style_name = styles->currentItem() ? styles->currentItem()->text() : QString();
		const QPixmap pixmap = render_preview(families->currentItem()->text(), style_name, format, twelve_hour);
		preview->setPixmap(pixmap);
	};

	const auto current = families->findItems(QString::fromStdString(face), Qt::MatchExactly);
	if (!current.isEmpty()) {
		families->setCurrentItem(current.first());
	} else if (families->count() > 0) {
		families->setCurrentRow(0);
	}

	auto *debounce = new QTimer(&dialog);
	debounce->setSingleShot(true);
	debounce->setInterval(100); // キーリピートなどでの連続更新を抑制する
	QObject::connect(debounce, &QTimer::timeout, preview, refresh_preview);
	QObject::connect(families, &QListWidget::currentTextChanged, debounce, qOverload<>(&QTimer::start));
	QObject::connect(styles, &QListWidget::currentTextChanged, debounce, qOverload<>(&QTimer::start));
	refresh_preview();

	if (dialog.exec() != QDialog::Accepted) {
		return false;
	}

	if (!families->currentItem()) {
		return false;
	}

	face = families->currentItem()->text().toStdString();
	style = styles->currentItem() ? styles->currentItem()->text().toStdString() : std::string();

	return true;
}
