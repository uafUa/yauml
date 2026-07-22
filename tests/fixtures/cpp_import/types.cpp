namespace cli_fixture {

struct Widget {
  int value;
  void reset();
};

class SpecializedWidget : public Widget {
public:
  void refresh();
};

} // namespace cli_fixture
