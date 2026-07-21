#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QString>

namespace uuml {

struct Json5Result {
  QJsonDocument document;
  QString error;
  bool hadComments = false;

  explicit operator bool() const {
    return error.isEmpty() && !document.isNull();
  }
};

class Json5 {
public:
  static Json5Result parse(const QByteArray &source);
};

} // namespace uuml
