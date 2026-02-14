#pragma once

#include <QStringList>

struct PresetMetadata {
  int rating = 3;
  bool favorite = false;
  QStringList tags;
};
