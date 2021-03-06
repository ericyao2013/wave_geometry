/**
 * @file
 * @author lkoppel
 */

#ifndef WAVE_GEOMETRY_RIGIDTRANSFORMBASE_HPP
#define WAVE_GEOMETRY_RIGIDTRANSFORMBASE_HPP

namespace wave {

/** Base class for proper rigid transformations in SE(3) */
template <typename Derived>
class RigidTransformBase : public TransformBase<Derived> {
 public:
    template <typename T>
    using BaseTmpl = RigidTransformBase<T>;
};

/** Transforms a point. Also known as a coordinate map.
 *
 * @f[ SO(3) \times R^3 \to R^3 @f]
 */
template <typename L, typename R>
auto operator*(const RigidTransformBase<L> &lhs, const TranslationBase<R> &rhs)
  -> Transform<L, R> {
    return Transform<L, R>{lhs.derived(), rhs.derived()};
}
// Overloads for rvalues
template <typename L, typename R>
auto operator*(RigidTransformBase<L> &&lhs, const TranslationBase<R> &rhs)
  -> Transform<internal::arg_t<L>, R> {
    return Transform<internal::arg_t<L>, R>{std::move(lhs).derived(), rhs.derived()};
}
template <typename L, typename R>
auto operator*(const RigidTransformBase<L> &lhs, TranslationBase<R> &&rhs)
  -> Transform<L, internal::arg_t<R>> {
    return Transform<L, internal::arg_t<R>>{lhs.derived(), std::move(rhs).derived()};
}
template <typename L, typename R>
auto operator*(RigidTransformBase<L> &&lhs, TranslationBase<R> &&rhs)
  -> Transform<internal::arg_t<L>, internal::arg_t<R>> {
    return Transform<internal::arg_t<L>, internal::arg_t<R>>{std::move(lhs).derived(),
                                                             std::move(rhs).derived()};
}


namespace internal {

/** Base for traits of a rigid transform leaf using an Eigen type for storage.*/
template <typename Derived>
struct rt_leaf_traits_base;

template <template <typename...> class Tmpl, typename ImplType_>
struct rt_leaf_traits_base<Tmpl<ImplType_>> : leaf_traits_base<Tmpl<ImplType_>>,
                                              frameable_transform_traits {
    template <typename NewImplType>
    using rebind = Tmpl<NewImplType>;

    using ImplType = ImplType_;
    using Scalar = typename ImplType::Scalar;
    using TangentType = Twist<Eigen::Matrix<Scalar, 6, 1>>;
    static constexpr int TangentSize = 6;

    // Each leaf must define its own PlainType. There is no consistent place to get it
    // because Eigen matrices have PlainObject in the class, Quaternion in traits, and
    // AngleAxis nowhere
};

/** Implementation of Random for a rigid transform
 *
 * Produces a transform with a random rotation on SO(3) and a translation with uniformly
 * random coefficients from -1 to 1
 */
template <typename Leaf, TICK_REQUIRES(internal::is_rt_leaf<Leaf>{})>
auto evalImpl(expr<Random, Leaf>) -> Leaf {
    using Scalar = internal::scalar_t<Leaf>;
    return Leaf{randomQuaternion<Scalar>(), Eigen::Matrix<Scalar, 3, 1>::Random()};
}

/** Implementation of Inverse for any rigid transform
 */
template <typename Rhs>
auto evalImpl(expr<Inverse>, const RigidTransformBase<Rhs> &rhs) -> plain_eval_t<Rhs> {
    return plain_eval_t<Rhs>{
      inverse(rhs.derived().rotation()),
      -(inverse(rhs.derived().rotation()) * rhs.derived().translation())};
}

/** Jacobian of Inverse for any rigid transform */
template <typename Val, typename Rhs>
auto jacobianImpl(expr<Inverse>,
                  const RigidTransformBase<Val> &val,
                  const RigidTransformBase<Rhs> &) -> jacobian_t<Val, Rhs> {
    using Scalar = scalar_t<Val>;
    using Mat3 = Eigen::Matrix<Scalar, 3, 3>;

    // The derivative of the inverse can be found by applying the adjoint identity
    // (see http://ethaneade.com/lie.pdf) to be negative adjoint of the inverted SE(3)
    // @todo factor out adjoint
    const auto &R = Mat3{val.derived().rotation().value()};
    const auto &t = val.derived().translation().value();
    jacobian_t<Val, Rhs> adj{};

    adj.template topLeftCorner<3, 3>() = R;
    adj.template bottomLeftCorner<3, 3>() = crossMatrix(t) * R;
    adj.template topRightCorner<3, 3>().setZero();
    adj.template bottomRightCorner<3, 3>() = R;

    return -adj;
}

/** Implementation of Compose for any rigid transform
 */
template <typename Lhs, typename Rhs>
auto evalImpl(expr<Compose>,
              const RigidTransformBase<Lhs> &lhs,
              const RigidTransformBase<Rhs> &rhs) -> plain_eval_t<Rhs> {
    plain_eval_t<Rhs> res{};
    res.rotation() = lhs.derived().rotation() * rhs.derived().rotation();
    res.translation() = lhs.derived().rotation() * rhs.derived().translation() +
                        lhs.derived().translation();
    return res;
}

/** Jacobian of Compose wrt any rigid transform rhs
 */
template <typename Val, typename Lhs, typename Rhs>
auto rightJacobianImpl(expr<Compose>,
                       const TransformBase<Val> &,
                       const TransformBase<Lhs> &lhs,
                       const TransformBase<Rhs> &) -> jacobian_t<Val, Lhs> {
    using Scalar = scalar_t<Val>;
    using Mat3 = Eigen::Matrix<Scalar, 3, 3>;

    // From http://ethaneade.com/lie.pdf - note we swap order of rotation and translation
    // Actually the adjoint of SE(3)
    // @todo factor out adjoint
    const auto &R = Mat3{lhs.derived().rotation().value()};
    const auto &t = lhs.derived().translation().value();
    jacobian_t<Val, Lhs> out{};

    out.template topLeftCorner<3, 3>() = R;
    out.template bottomLeftCorner<3, 3>() = crossMatrix(t) * R;
    out.template topRightCorner<3, 3>().setZero();
    out.template bottomRightCorner<3, 3>() = R;

    return out;
}

/** Implements Transform for any rigid transform
 *
 * More efficient implementations may be available for specific types (e.g. 4x4 matrix)
 */
template <typename Lhs, typename Rhs>
auto evalImpl(expr<Transform>,
              const TransformBase<Lhs> &lhs,
              const TranslationBase<Rhs> &rhs) -> plain_eval_t<Rhs> {
    return eval(lhs.derived().rotation() * rhs.derived() + lhs.derived().translation());
}

/** Jacobian of any Transform wrt to the lhs */
template <typename Val, typename Lhs, typename Rhs>
auto leftJacobianImpl(expr<Transform>,
                      const TranslationBase<Val> &val,
                      const TransformBase<Lhs> &,
                      const TranslationBase<Rhs> &) -> jacobian_t<Val, Lhs> {
    using Scalar = scalar_t<Val>;

    // From http://ethaneade.com/lie.pdf - note we swap order of rotation and translation
    jacobian_t<Val, Lhs> out{};
    out << crossMatrix(-val.derived().value()), IdentityMatrix<Scalar, 3>{};
    return out;
}

/** Jacobian of any Transform wrt to the rhs is the rotation part */
template <typename Val, typename Lhs, typename Rhs>
auto rightJacobianImpl(expr<Transform>,
                       const TranslationBase<Val> &,
                       const TransformBase<Lhs> &lhs,
                       const TranslationBase<Rhs> &) -> jacobian_t<Val, Rhs> {
    return jacobian_t<Val, Rhs>{lhs.derived().rotation().value()};
}

/** Implementation of LogMap for any rigid transform
 */
template <typename Rhs>
auto evalImpl(expr<LogMap>, const RigidTransformBase<Rhs> &rhs) ->
  typename traits<Rhs>::TangentType {
    using Scalar = scalar_t<Rhs>;
    using Vec3 = Eigen::Matrix<Scalar, 3, 1>;
    using Mat3 = Eigen::Matrix<Scalar, 3, 3>;

    // Logmap of rotation part: delegate to RelativeRotation code
    const Vec3 omega = eval(log(rhs.derived().rotation())).value();

    // Logmap of translation part: not trivial (see http://ethaneade.com/lie.pdf)
    // @todo optimize
    Vec3 ln_t;
    const auto theta2 = omega.squaredNorm();
    const auto theta = std::sqrt(theta2);
    if (theta2 > Eigen::NumTraits<Scalar>::epsilon()) {
        const auto A = std::sin(theta) / theta;
        const auto B = (1 - std::cos(theta)) / theta2;
        const Mat3 cross = crossMatrix(omega);
        const Mat3 cross2 = cross * cross;

        const Mat3 Vinv =
          Mat3::Identity() - cross / 2 + (1 - A / 2 / B) / theta2 * cross2;

        ln_t = Vinv * rhs.derived().translation().value();

    } else {
        // small theta2; use limit as theta -> 0
        // @todo: use Taylor series, not just the limit!
        const Mat3 cross = crossMatrix(omega);

        const Mat3 Vinv = Mat3::Identity() - cross / 2;

        ln_t = Vinv * rhs.derived().translation().value();
    }
    return typename traits<Rhs>::TangentType{omega, ln_t};
}

/** Jacobian of LogMap for any rigid transform */
template <typename Val, typename Rhs>
auto jacobianImpl(expr<LogMap>,
                  const TwistBase<Val> &val,
                  const RigidTransformBase<Rhs> &rhs) -> jacobian_t<Val, Rhs> {
    using Scalar = scalar_t<Val>;
    using Mat3 = Eigen::Matrix<Scalar, 3, 3>;

    // From http://ethaneade.org/exp_diff.pdf - note we swap order of rotation and
    // translation

    using std::cos;
    using std::sin;
    // First get Jacobian of logmap of rotation part only
    const auto &Drot =
      jacobianImpl(expr<LogMap>{}, val.derived().rotation(), rhs.derived().rotation());

    const auto &omega = val.derived().rotation().value();
    const auto &u = val.derived().translation().value();
    const auto theta2 = omega.squaredNorm();
    const auto theta = sqrt(theta2);
    const auto a = sin(theta) / theta;
    const auto b = (1 - cos(theta)) / theta2;
    const auto c = (1 - a) / theta2;
    // Calculate Eade's "W" term
    const Mat3 W = (c - b) * Mat3::Identity() +
                   (a - 2 * b) / theta2 * crossMatrix(omega) +
                   (b - 3 * c) / theta2 * omega * omega.transpose();

    // Calculate Eade's "B" term
    const Mat3 B = b * crossMatrix(u) +
                   c * (omega * u.transpose() + u * omega.transpose()) + omega.dot(u) * W;


    Eigen::Matrix<Scalar, 6, 6> out{};
    out.template topLeftCorner<3, 3>() = Drot;                 // R wrt R
    out.template bottomLeftCorner<3, 3>() = -Drot * B * Drot;  // t wrt R
    out.template topRightCorner<3, 3>().setZero();             // R wrt t
    out.template bottomRightCorner<3, 3>() = Drot;             // t wrt t
    return out;
}


/** Gives .rotation() and .translation() methods to Framed<> versions of rigid transforms
 */
template <typename Leaf, typename LeftFrame, typename RightFrame>
struct FramedLeafAccess<Framed<Leaf, LeftFrame, RightFrame>,
                        TICK_CLASS_REQUIRES(internal::is_rt_leaf<Leaf>{})>
  : FramedLeafAccessBase<Framed<Leaf, LeftFrame, RightFrame>> {
    using RotationType = decltype(std::declval<Leaf>().rotation());
    using ConstRotationType = decltype(std::declval<const Leaf>().rotation());
    using TranslationType = decltype(std::declval<Leaf>().translation());
    using ConstTranslationType = decltype(std::declval<const Leaf>().translation());

 public:
    /** Returns a mutable expression referring to the translation portion of this
    * transform */
    auto rotation() &
      noexcept ->
      typename internal::add_frames<LeftFrame, RightFrame>::template to<RotationType> {
        return internal::WrapWithFrames<LeftFrame, RightFrame>{}(this->leaf().rotation());
    }

    /** Returns a mutable expression referring to the translation portion of this
    * transform */
    auto rotation() &&
      noexcept ->
      typename internal::add_frames<LeftFrame, RightFrame>::template to<RotationType> {
        return internal::WrapWithFrames<LeftFrame, RightFrame>{}(this->leaf().rotation());
    }

    /** Returns an expression referring to the rotation portion of this transform */
    auto rotation() const &
      noexcept ->
      typename internal::add_frames<LeftFrame,
                                    RightFrame>::template to<ConstRotationType> {
        return internal::WrapWithFrames<LeftFrame, RightFrame>{}(this->leaf().rotation());
    }

    /** Returns a mutable expression referring to the translation portion of this
     * transform */
    auto translation() noexcept ->
      typename internal::add_frames<LeftFrame, LeftFrame, RightFrame>::template to<
        TranslationType> {
        return internal::WrapWithFrames<LeftFrame, LeftFrame, RightFrame>{}(
          this->leaf().translation());
    }

    /** Returns an expression referring to the translation portion of this transform */
    auto translation() const noexcept ->
      typename internal::add_frames<LeftFrame, LeftFrame, RightFrame>::template to<
        ConstTranslationType> {
        return internal::WrapWithFrames<LeftFrame, LeftFrame, RightFrame>{}(
          this->leaf().translation());
    }
};

}  // namespace internal
}  // namespace wave

#endif  // WAVE_GEOMETRY_RIGIDTRANSFORMBASE_HPP
